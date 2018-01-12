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

#include "path_tracing/space/parallelotope_algorithm.h"
#include "path_tracing/space/shape_intersection.h"

// Для функций shape_intersection при построении дерева (октадерево и т.п.), а также для самого
// дерева нужны функции intersect, inside (если объект имеет объём), vertices и vertex_ridges.
// Функции vertices и vertex_ridges с их массивами становятся ненужными после построения дерева.

template <typename Parallelotope>
class ParallelotopeWrapperForShapeIntersection final
{
        using VertexRidges = typename ParallelotopeAlgorithm<Parallelotope>::VertexRidges;
        using Vertices = typename ParallelotopeAlgorithm<Parallelotope>::Vertices;

        const Parallelotope& m_parallelotope;

        Vertices m_vertices;
        VertexRidges m_vertex_ridges;

public:
        static constexpr size_t DIMENSION = Parallelotope::DIMENSION;
        using DataType = typename Parallelotope::DataType;

        static constexpr size_t SHAPE_DIMENSION = DIMENSION;

        ParallelotopeWrapperForShapeIntersection(const Parallelotope& p)
                : m_parallelotope(p), m_vertices(parallelotope_vertices(p)), m_vertex_ridges(parallelotope_vertex_ridges(p))
        {
        }

        bool intersect(const Ray<DIMENSION, DataType>& r, DataType* t) const
        {
                return m_parallelotope.intersect(r, t);
        }

        bool inside(const Vector<DIMENSION, DataType>& p) const
        {
                return m_parallelotope.inside(p);
        }

        const Vertices& vertices() const
        {
                return m_vertices;
        }

        const VertexRidges& vertex_ridges() const
        {
                return m_vertex_ridges;
        }
};