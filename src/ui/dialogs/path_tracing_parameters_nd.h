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

#pragma once

#include "ui_path_tracing_parameters_nd.h"

#include <string>

class PathTracingParametersForNd final : public QDialog
{
        Q_OBJECT

public:
        explicit PathTracingParametersForNd(QWidget* parent = nullptr);

        [[nodiscard]] bool show(const std::string& description, int max_thread_count, int default_screen_size,
                                int min_screen_size, int max_screen_size, int default_samples_per_pixel,
                                int max_samples_per_pixel, int* thread_count, int* min_size, int* max_size,
                                int* samples_per_pixel);

private slots:
        void on_spinBox_min_size_valueChanged(int);
        void on_spinBox_max_size_valueChanged(int);

private:
        int m_max_thread_count;
        int m_min_screen_size;
        int m_max_screen_size;
        int m_max_samples_per_pixel;

        int m_thread_count;
        int m_min_size;
        int m_max_size;
        int m_samples_per_pixel;

        Ui::PathTracingParametersForNd ui;

        void done(int r) override;
};