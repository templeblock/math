/*
Copyright (C) 2017-2019 Topological Manifold

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

#include "com/error.h"
#include "com/vec.h"

#include <iterator>

template <typename Container, typename T, size_t N>
void vertex_min_max(const Container& vertices, Vector<N, T>* min, Vector<N, T>* max)
{
        if (std::empty(vertices))
        {
                error("No vertex for minimum and maximum");
        }

        auto i = std::cbegin(vertices);

        *min = *i;
        *max = *i;

        while (++i != std::cend(vertices))
        {
                *min = min_vector(*i, *min);
                *max = max_vector(*i, *max);
        }
}
