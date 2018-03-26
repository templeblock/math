/*
Copyright (C) 2017, 2018 Topological Manifold

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "painter_window.h"

#include "com/alg.h"
#include "com/error.h"
#include "com/log.h"
#include "com/print.h"
#include "com/time.h"
#include "ui/dialogs/message_box.h"

#include <QFileDialog>
#include <cstring>

constexpr int PANTBRUSH_WIDTH = 20;
constexpr int UPDATE_INTERVAL_MILLISECONDS = 100;
// Этот интервал должен быть больше интервала UPDATE_INTERVAL_MILLISECONDS
constexpr int DIFFERENCE_INTERVAL_MILLISECONDS = 10 * UPDATE_INTERVAL_MILLISECONDS;

constexpr bool SHOW_THREADS = true;

namespace
{
void set_label_minimum_width_for_text(QLabel* label, const std::string& text)
{
        label->setMinimumWidth(label->fontMetrics().width(text.c_str()));
}

void set_text_and_minimum_width(QLabel* label, const std::string& text)
{
        label->setText(text.c_str());
        label->setMinimumWidth(std::max(label->width(), label->fontMetrics().width(text.c_str())));
}

template <size_t N, typename T>
std::vector<T> array_to_vector(const std::array<T, N>& array)
{
        std::vector<T> vector(N);
        std::copy(array.cbegin(), array.cend(), vector.begin());
        return vector;
}
}

class PainterWindowUI::Difference
{
        struct Point
        {
                std::array<long long, 3> data;
                double time;
                Point(std::array<long long, 3> data_, double time_) : data(data_), time(time_)
                {
                }
        };

        const double m_interval_seconds;
        std::deque<Point> m_deque;

public:
        Difference(int interval_milliseconds) : m_interval_seconds(interval_milliseconds / 1000.0)
        {
        }

        std::tuple<long long, long long, long long, double> compute(const std::array<long long, 3>& data)
        {
                double time = time_in_seconds();

                // Удаление старых элементов
                while (!m_deque.empty() && m_deque.front().time < time - m_interval_seconds)
                {
                        m_deque.pop_front();
                }

                m_deque.emplace_back(data, time);

                return std::make_tuple(
                        m_deque.back().data[0] - m_deque.front().data[0], m_deque.back().data[1] - m_deque.front().data[1],
                        m_deque.back().data[2] - m_deque.front().data[2], m_deque.back().time - m_deque.front().time);
        }
};

PainterWindowUI::PainterWindowUI(const std::string& title, std::vector<int>&& screen_size)
        : m_screen_size(std::move(screen_size)),
          m_width(m_screen_size[0]),
          m_height(m_screen_size[1]),
          m_image(m_width, m_height, QImage::Format_RGB32),
          m_image_byte_count(m_width * m_height * sizeof(quint32)),
          m_first_show(true),
          m_difference(std::make_unique<Difference>(DIFFERENCE_INTERVAL_MILLISECONDS))
{
        ui.setupUi(this);

        this->setWindowTitle(title.c_str());

        connect(this, SIGNAL(error_message_signal(QString)), this, SLOT(error_message_slot(QString)),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));

        connect(&m_timer, SIGNAL(timeout()), this, SLOT(timer_slot()));

        ASSERT(m_image.byteCount() == m_image_byte_count);
}

PainterWindowUI::~PainterWindowUI() = default;

void PainterWindowUI::init_window()
{
        set_data_vectors();
        set_interface();

        set_slice_offset();

        update_points();

        m_timer.start(UPDATE_INTERVAL_MILLISECONDS);
}

void PainterWindowUI::set_interface()
{
        ui.label_points->setText("");
        ui.label_points->resize(m_width, m_height);

        ui.label_rays_per_second->setText("");
        ui.label_ray_count->setText("");
        ui.label_pass_count->setText("");
        ui.label_samples_per_pixel->setText("");

        ui.scrollAreaWidgetContents->layout()->setContentsMargins(0, 0, 0, 0);
        ui.scrollAreaWidgetContents->layout()->setSpacing(0);
        this->layout()->setContentsMargins(5, 5, 5, 5);

        ui.checkBox_show_threads->setChecked(SHOW_THREADS);

        const int slider_count = static_cast<int>(m_screen_size.size()) - 2;

        if (slider_count <= 0)
        {
                return;
        }

        m_dimension_sliders.resize(slider_count);

        QWidget* layout_widget = new QWidget(this);
        this->layout()->addWidget(layout_widget);

        QGridLayout* layout = new QGridLayout(layout_widget);
        layout_widget->setLayout(layout);
        layout->setContentsMargins(0, 0, 0, 0);

        for (int i = 0; i < slider_count; ++i)
        {
                constexpr int init_value = 0;

                int dimension = i + 2;
                int dimension_max_value = m_screen_size[dimension] - 1;

                m_dimension_sliders[i].slider.setOrientation(Qt::Horizontal);
                m_dimension_sliders[i].slider.setMinimum(0);
                m_dimension_sliders[i].slider.setMaximum(dimension_max_value);
                m_dimension_sliders[i].slider.setValue(init_value);
                set_label_minimum_width_for_text(&m_dimension_sliders[i].label, to_string_digit_groups(dimension_max_value));
                m_dimension_sliders[i].label.setText(to_string(init_value).c_str());

                QLabel* label_d = new QLabel(QString("d[") + to_string(dimension).c_str() + "]", layout_widget);
                QLabel* label_e = new QLabel("=", layout_widget);

                layout->addWidget(label_d, i, 0);
                layout->addWidget(label_e, i, 1);
                layout->addWidget(&m_dimension_sliders[i].label, i, 2);
                layout->addWidget(&m_dimension_sliders[i].slider, i, 3);

                connect(&m_dimension_sliders[i].slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed_slot(int)));
        }
}

void PainterWindowUI::set_data_vectors()
{
        long long pixel_count = multiply_all<long long>(m_screen_size);

        m_data.resize(pixel_count);
        m_data_clean.resize(pixel_count);

        set_default_pixels();
}

void PainterWindowUI::set_default_pixels()
{
        static_assert(sizeof(QRgb) == sizeof(quint32));
        ASSERT(m_data.size() == m_data_clean.size());

        unsigned slice_size = m_width * m_height;

        ASSERT(m_data.size() >= slice_size);
        ASSERT(m_data.size() % slice_size == 0);

        long long slice_count = m_data.size() / slice_size;
        long long index = 0;
        for (long long slice = 0; slice < slice_count; ++slice)
        {
                for (int y = 0; y < m_height; ++y)
                {
                        for (int x = 0; x < m_width; ++x)
                        {
                                quint32 v = ((x + y) & 1) ? qRgba(100, 150, 200, 0) : 0;
                                m_data[index] = v;
                                m_data_clean[index] = v;
                                ++index;
                        }
                }
        }

        ASSERT(index == static_cast<long long>(m_data.size()));
}

void PainterWindowUI::set_slice_offset()
{
        std::vector<int> slider_positions(m_dimension_sliders.size());

        for (unsigned i = 0; i < m_dimension_sliders.size(); ++i)
        {
                slider_positions[i] = m_dimension_sliders[i].slider.value();
        }

        m_slice_offset = slice_offset(slider_positions);
}

void PainterWindowUI::set_pixel(long long index, unsigned char r, unsigned char g, unsigned char b) noexcept
{
        quint32 c = (r << 16) + (g << 8) + b;

        m_data[index] = c;
        m_data_clean[index] = c;
}

void PainterWindowUI::mark_pixel_busy(long long index) noexcept
{
        m_data[index] ^= 0x00ff'ffff;
}

void PainterWindowUI::update_points()
{
        const quint32* image_data = ui.checkBox_show_threads->isChecked() ? m_data.data() : m_data_clean.data();
        std::memcpy(m_image.bits(), image_data + m_slice_offset, m_image_byte_count);
        ui.label_points->setPixmap(QPixmap::fromImage(m_image));
        ui.label_points->update();
}

void PainterWindowUI::error_message(const std::string& msg) const noexcept
{
        try
        {
                emit error_message_signal(msg.c_str());
        }
        catch (...)
        {
                error_fatal("Error painter message emit signal");
        }
}

void PainterWindowUI::error_message_slot(QString msg)
{
        LOG("Painter error\n" + msg.toStdString());

        message_critical(this, msg);
}

void PainterWindowUI::showEvent(QShowEvent* e)
{
        QWidget::showEvent(e);

        if (!m_first_show)
        {
                return;
        }
        m_first_show = false;

        QTimer::singleShot(50, this, SLOT(first_shown()));
}

void PainterWindowUI::first_shown()
{
        ui.scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui.scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        resize(QSize(2 + m_width, 2 + m_height) + (geometry().size() - ui.scrollArea->size()));

        ui.scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui.scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void PainterWindowUI::timer_slot()
{
        long long pass_count, pixel_count, ray_count, sample_count;
        double previous_pass_duration;

        painter_statistics(&pass_count, &pixel_count, &ray_count, &sample_count, &previous_pass_duration);

        auto[ray_diff, sample_diff, pixel_diff, time_diff] = m_difference->compute({{ray_count, sample_count, pixel_count}});

        long long rays_per_second = time_diff != 0 ? std::llround(ray_diff / time_diff) : 0;
        long long samples_per_pixel = pixel_diff != 0 ? std::llround(static_cast<double>(sample_diff) / pixel_diff) : 0;

        long long milliseconds_per_frame = std::llround(1000 * previous_pass_duration);

        set_text_and_minimum_width(ui.label_rays_per_second, to_string_digit_groups(rays_per_second));
        set_text_and_minimum_width(ui.label_ray_count, to_string_digit_groups(ray_count));
        set_text_and_minimum_width(ui.label_pass_count, to_string_digit_groups(pass_count));
        set_text_and_minimum_width(ui.label_samples_per_pixel, to_string_digit_groups(samples_per_pixel));
        set_text_and_minimum_width(ui.label_milliseconds_per_frame, to_string_digit_groups(milliseconds_per_frame));

        update_points();
}

void PainterWindowUI::on_pushButton_save_to_file_clicked()
{
        QString file_name = QFileDialog::getSaveFileName(this, "Export to file", "", "Images (*.png)", nullptr,
                                                         QFileDialog::DontUseNativeDialog);
        if (file_name.size() == 0)
        {
                return;
        }

        // Таймер и эта функция работают в одном потоке, поэтому можно пользоваться
        // переменной m_image без блокировок.
        std::memcpy(m_image.bits(), m_data_clean.data() + m_slice_offset, m_image_byte_count);
        if (!m_image.save(file_name, "PNG"))
        {
                error_message("Error saving image to file");
        }
}

void PainterWindowUI::slider_changed_slot(int)
{
        QObject* s = sender();
        for (DimensionSlider& dm : m_dimension_sliders)
        {
                if (&dm.slider == s)
                {
                        set_text_and_minimum_width(&dm.label, to_string_digit_groups(dm.slider.value()));
                        set_slice_offset();
                        return;
                }
        }
        error_message("Failed to find sender in sliders");
}

//
//
//

template <size_t N, typename T>
long long PainterWindow<N, T>::pixel_index(const std::array<int_least16_t, N_IMAGE>& pixel) const noexcept
{
        return m_global_index.compute(pixel);
}

template <size_t N, typename T>
void PainterWindow<N, T>::painter_statistics(long long* pass_count, long long* pixel_count, long long* ray_count,
                                             long long* sample_count, double* previous_pass_duration) const noexcept
{
        m_paintbrush.statistics(pass_count, pixel_count, ray_count, sample_count, previous_pass_duration);
}

template <size_t N, typename T>
long long PainterWindow<N, T>::slice_offset(const std::vector<int>& slider_positions) const
{
        ASSERT(slider_positions.size() + 2 == N_IMAGE);

        std::array<int_least16_t, N_IMAGE> pixel;

        pixel[0] = 0;
        pixel[1] = 0;

        for (unsigned i = 0; i < slider_positions.size(); ++i)
        {
                int dimension = i + 2;
                pixel[dimension] = slider_positions[i];

                ASSERT(pixel[dimension] >= 0 && pixel[dimension] < m_paint_objects->projector().screen_size()[dimension]);
        }

        return pixel_index(pixel);
}

template <size_t N, typename T>
void PainterWindow<N, T>::painter_pixel_before(const std::array<int_least16_t, N_IMAGE>& pixel) noexcept
{
        std::array<int_least16_t, N_IMAGE> p = pixel;
        p[1] = m_height - 1 - pixel[1];

        PainterWindowUI::mark_pixel_busy(pixel_index(p));
}

template <size_t N, typename T>
void PainterWindow<N, T>::painter_pixel_after(const std::array<int_least16_t, N_IMAGE>& pixel, const SrgbInteger& c) noexcept
{
        std::array<int_least16_t, N_IMAGE> p = pixel;
        p[1] = m_height - 1 - pixel[1];

        PainterWindowUI::set_pixel(pixel_index(p), c.red, c.green, c.blue);
}

template <size_t N, typename T>
void PainterWindow<N, T>::painter_error_message(const std::string& msg) noexcept
{
        PainterWindowUI::error_message(msg);
}

template <size_t N, typename T>
PainterWindow<N, T>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                   std::unique_ptr<const PaintObjects<N, T>>&& paint_objects)
        : PainterWindowUI(title, array_to_vector(paint_objects->projector().screen_size())),
          m_paint_objects(std::move(paint_objects)),
          m_global_index(m_paint_objects->projector().screen_size()),
          m_height(m_paint_objects->projector().screen_size()[1]),
          m_samples_per_pixel(samples_per_pixel),
          m_thread_count(thread_count),
          m_window_thread_id(std::this_thread::get_id()),
          m_paintbrush(m_paint_objects->projector().screen_size(), PANTBRUSH_WIDTH, -1),
          m_stop(false),
          m_thread_working(false)
{
        // Здесь могут быть вызовы виртуальных функций из класса UI,
        // поэтому всё должно быть готово для их вызовов
        PainterWindowUI::init_window();

        // Запуск потоков только после инициализации класса UI,
        // так как потоки вызывают функции класса UI
        m_stop = false;
        m_thread_working = true;
        m_thread = std::thread([this]() noexcept {
                paint(this, m_samples_per_pixel, *m_paint_objects, &m_paintbrush, m_thread_count, &m_stop);
                m_thread_working = false;
        });
}

template <size_t N, typename T>
PainterWindow<N, T>::~PainterWindow()
{
        ASSERT(std::this_thread::get_id() == m_window_thread_id);

        m_stop = true;

        if (m_thread.joinable())
        {
                m_thread.join();
        }
}

template PainterWindow<3, float>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                std::unique_ptr<const PaintObjects<3, float>>&& paint_objects);
template PainterWindow<4, float>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                std::unique_ptr<const PaintObjects<4, float>>&& paint_objects);
template PainterWindow<5, float>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                std::unique_ptr<const PaintObjects<5, float>>&& paint_objects);
template PainterWindow<6, float>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                std::unique_ptr<const PaintObjects<6, float>>&& paint_objects);

template PainterWindow<3, double>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                 std::unique_ptr<const PaintObjects<3, double>>&& paint_objects);
template PainterWindow<4, double>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                 std::unique_ptr<const PaintObjects<4, double>>&& paint_objects);
template PainterWindow<5, double>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                 std::unique_ptr<const PaintObjects<5, double>>&& paint_objects);
template PainterWindow<6, double>::PainterWindow(const std::string& title, unsigned thread_count, int samples_per_pixel,
                                                 std::unique_ptr<const PaintObjects<6, double>>&& paint_objects);
