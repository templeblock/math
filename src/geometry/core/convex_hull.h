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

#include "com/vec.h"
#include "progress/progress.h"

#include <array>
#include <list>
#include <vector>

template <size_t N>
class ConvexHullFacet
{
        const std::array<int, N> m_indices;
        const vec<N> m_ortho;

public:
        ConvexHullFacet(const std::array<int, N>& indices, const vec<N>& ortho) : m_indices(indices), m_ortho(ortho)
        {
        }
        const std::array<int, N>& vertices() const
        {
                return m_indices;
        }
        const vec<N>& ortho() const
        {
                return m_ortho;
        }
};

template <size_t N>
class DelaunaySimplex
{
        const std::array<int, N + 1> m_indices;
        const std::array<vec<N>, N + 1> m_orthos;

public:
        DelaunaySimplex(const std::array<int, N + 1>& indices, const std::array<vec<N>, N + 1>& orthos)
                : m_indices(indices), m_orthos(orthos)
        {
        }
        const std::array<int, N + 1>& vertices() const
        {
                return m_indices;
        }
        const vec<N>& ortho(unsigned i) const
        {
                ASSERT(i < m_orthos.size());
                return m_orthos[i];
        }
};

template <size_t N>
void compute_delaunay(const std::vector<Vector<N, float>>& source_points, std::vector<vec<N>>* points,
                      std::vector<DelaunaySimplex<N>>* simplices, ProgressRatio* progress);
template <size_t N>
void compute_convex_hull(const std::vector<Vector<N, float>>& source_points, std::vector<ConvexHullFacet<N>>* ch_facets,
                         ProgressRatio* progress);
