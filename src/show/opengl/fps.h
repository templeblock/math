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

#include <array>
#include <deque>

class FPS
{
        // Интервал в секундах
        static constexpr double INTERVAL_LENGTH = 1;

        // Сколько отсчётов на интервале, не считая текущего
        static constexpr int INTERVAL_SAMPLE_COUNT = 10;

        struct Frames
        {
                int time;
                double fps = 0;
                Frames(int time_) : time(time_)
                {
                }
        };

        static std::array<double, INTERVAL_SAMPLE_COUNT> window_function();

        const std::array<double, INTERVAL_SAMPLE_COUNT> m_window;

        std::deque<Frames> m_deque;

public:
        FPS();
        long calculate();
};
