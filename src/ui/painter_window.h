/*
Copyright (C) 2017 Topological Manifold

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

#pragma once

#include "ui_painter_window.h"

#include "path_tracing/objects.h"
#include "path_tracing/painter.h"
#include "path_tracing/pixel_sequence.h"

#include <QTimer>
#include <thread>

class PainterWindow final : public QWidget, public IPainterNotifier
{
        Q_OBJECT

public:
        PainterWindow(const PaintObjects* paint_objects, unsigned thread_count);
        ~PainterWindow() override;

signals:
        void error_message_signal(QString) const;

private slots:
        void timer_slot();
        void first_shown();
        void error_message_slot(QString);

private:
        void showEvent(QShowEvent* event) override;

        void painter_pixel_before(int x, int y) noexcept override;
        void painter_pixel_after(int x, int y, unsigned char r, unsigned char g, unsigned char b) noexcept override;
        void painter_error_message(const std::string& msg) noexcept override;

        void set_pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b) noexcept;
        void xor_pixel(int x, int y) noexcept;
        void update_points() noexcept;

        const PaintObjects* m_paint_objects;
        unsigned m_thread_count;
        int m_width, m_height;
        QImage m_image;
        std::vector<quint32> m_data;
        QTimer m_timer;
        bool m_first_show;
        std::atomic_bool m_stop;
        std::atomic_ullong m_ray_count;
        std::thread m_thread;
        std::atomic_bool m_thread_working;
        const std::thread::id m_window_thread_id;
        Paintbrush m_paintbrush;

        Ui::PainterWindow ui;
};

void create_painter_window(const PaintObjects* paint_objects, unsigned thread_count);
