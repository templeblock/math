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

#include "ui_main_window.h"

#include "event_emitter.h"

#include "progress/progress_list.h"
#include "show/show.h"
#include "test/self_test.h"

#include <QColor>
#include <QProgressBar>
#include <QTimer>
#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <size_t N>
struct IManifoldConstructor;

template <size_t N>
struct IObjectRepository;

class MainWindow final : public QMainWindow
{
        Q_OBJECT

public:
        explicit MainWindow(QWidget* parent = nullptr);
        ~MainWindow() override;

protected:
        void showEvent(QShowEvent* e) override;

public slots:

private slots:
        void on_actionLoad_triggered();
        void on_actionExport_triggered();
        void on_actionExit_triggered();
        void on_actionHelp_triggered();
        void on_actionSelfTest_triggered();
        void on_actionAbout_triggered();
        void on_actionFullScreen_triggered();
        void on_Slider_Ambient_valueChanged(int value);
        void on_Slider_Diffuse_valueChanged(int value);
        void on_Slider_Specular_valueChanged(int value);
        void on_Slider_DFT_Brightness_valueChanged(int value);
        void on_Slider_Default_Ns_valueChanged(int value);
        void on_ButtonBackgroundColor_clicked();
        void on_ButtonDefaultColor_clicked();
        void on_ButtonWireframeColor_clicked();
        void on_Button_LoadBoundCocone_clicked();
        void on_Button_ResetView_clicked();
        void on_checkBox_Wireframe_clicked();
        void on_checkBox_Materials_clicked();
        void on_checkBox_Shadow_clicked();
        void on_checkBox_Smooth_clicked();
        void on_checkBox_ShowEffect_clicked();
        void on_checkBox_show_dft_clicked();
        void on_checkBox_convex_hull_2d_clicked();
        void on_checkBox_OpticalFlow_clicked();
        void on_radioButton_Model_clicked();
        void on_radioButton_ModelConvexHull_clicked();
        void on_radioButton_Cocone_clicked();
        void on_radioButton_CoconeConvexHull_clicked();
        void on_radioButton_BoundCocone_clicked();
        void on_radioButton_BoundCoconeConvexHull_clicked();

        void slot_object_repository();
        void slot_window_event(const WindowEvent&);
        void slot_timer_progress_bar();
        void slot_window_first_shown();
        void slot_widget_under_window_mouse_wheel(double delta);
        void slot_widget_under_window_resize();

private:
        enum class OpenObjectType
        {
                File,
                Repository
        };

        void set_dependent_interface();

        static void strike_out_radio_button(QRadioButton* button);
        static void enable_radio_button(QRadioButton* button);
        void strike_out_all_objects_buttons();
        void strike_out_bound_cocone_buttons();

        void thread_open_object(const std::string& object_name, OpenObjectType object_type,
                                ProgressRatioList* progress_ratio_list) noexcept;
        void thread_model(std::shared_ptr<IObj> obj, ProgressRatioList* progress_ratio_list) noexcept;
        void thread_surface_constructor(ProgressRatioList* progress_ratio_list) noexcept;
        void thread_cocone(ProgressRatioList* progress_ratio_list) noexcept;
        void thread_bound_cocone(double rho, double alpha, ProgressRatioList* progress_ratio_list) noexcept;
        void thread_self_test(SelfTestType test_type, ProgressRatioList* progress_ratio_list) noexcept;

        void start_thread_open_object(const std::string& object_name, OpenObjectType object_type);
        void start_thread_bound_cocone(double rho, double alpha);
        void start_thread_self_test(SelfTestType test_type);

        void stop_thread_open_object();
        void stop_thread_bound_cocone();
        void stop_thread_self_test();

        template <typename T>
        void catch_all(T&& a) noexcept;

        std::string object_threads_busy() const;

        void progress_bars(bool permanent, ProgressRatioList* progress_ratio_list, std::list<QProgressBar>* progress_bars);

        double get_ambient() const;
        double get_diffuse() const;
        double get_specular() const;
        double get_dft_brightness() const;
        double get_default_ns() const;

        void set_bound_cocone_parameters(double rho, double alpha);

        void set_clear_color(const QColor& c);
        void set_default_color(const QColor& c);
        void set_wireframe_color(const QColor& c);

        Ui::MainWindow ui;

        WindowEventEmitter m_event_emitter;

        std::unique_ptr<IShow> m_show;

        QColor m_clear_color;
        QColor m_default_color;
        QColor m_wireframe_color;

        bool m_first_show = true;

        QTimer m_timer_progress_bar;

        const std::thread::id m_thread_id;

        ProgressRatioList m_progress_ratio_list_open_object;
        std::list<QProgressBar> m_progress_bars_open_object;
        ProgressRatioList m_progress_ratio_list_bound_cocone;
        std::list<QProgressBar> m_progress_bars_bound_cocone;
        ProgressRatioList m_progress_ratio_list_self_test;
        std::list<QProgressBar> m_progress_bars_self_test;

        std::thread m_thread_open_object;
        std::thread m_thread_bound_cocone;
        std::thread m_thread_self_test;

        std::atomic_bool m_working_open_object = false;
        std::atomic_bool m_working_bound_cocone = false;
        std::atomic_bool m_working_self_test = false;

        std::vector<glm::vec3> m_surface_points;
        std::unique_ptr<IManifoldConstructor<3>> m_surface_constructor;

        double m_bound_cocone_rho;
        double m_bound_cocone_alpha;

        std::shared_ptr<IObj> m_surface_cocone;
        std::shared_ptr<IObj> m_surface_bound_cocone;

        std::unique_ptr<IObjectRepository<3>> m_object_repository;
        std::unordered_map<QObject*, std::string> m_action_to_object_name_map;
};
