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

/*
 Описание барицентрических координат имеется в книге
 Samuel R. Buss.
 3D Computer Graphics. A Mathematical Introduction with OpenGL.
 Cambridge University Press, 2003.
*/

#pragma once

#include "com/error.h"
#include "com/ray.h"
#include "com/types.h"
#include "com/vec.h"
#include "geometry/core/linear_algebra.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace SimplexGeometryImplementation
{
template <size_t N, typename T>
bool plane_intersect(const Ray<N, T>& ray, const Vector<N, T>& plane_point, const Vector<N, T>& plane_normal, T* t)
{
        T s = dot(plane_normal, ray.get_dir());

        if (s != 0)
        {
                *t = dot(plane_point - ray.get_org(), plane_normal) / s;
                return *t > 0;
        }

        return false;
}
}

template <size_t N, typename T>
class SimplexGeometry
{
        static_assert(N >= 2);
        static_assert(is_floating_point<T>);

        // (N-1)-мерные плоскости, перпендикулярные (N-1)-симплексу и проходящие через его грани,
        // за исключением одной грани.
        // Хранятся только данные плоскостей. Перпендикуляр и точки симплекса передаются
        // в параметрах функций, так как они имеются в структурах данных самого симплекса.
        struct
        {
                Vector<N, T> n;
                T d;

        } m_planes[N - 1];

        static T last_coordinate(const Vector<N - 1, T>& coordinates)
        {
                T r = 1;
                for (unsigned i = 0; i < N - 1; ++i)
                {
                        r -= coordinates[i];
                }
                return r;
        }

public:
        void set_data(Vector<N, T> simplex_normal, const std::array<Vector<N, T>, N>& vertices)
        {
                std::array<Vector<N, T>, N - 1> vectors;
                for (unsigned i = 0; i < N - 1; ++i)
                {
                        minus(&vectors[i], vertices[i], vertices[N - 1]);
                }

                // Перпендикуляр к симплексу берётся готовым в параметре simplex_normal
                // и должен быть равен ortho_nn(vectors).

                // Найти уравнения плоскостей, проходящих через каждую грань и перпендикулярных симплексу.
                // Вершина должна находиться на расстоянии 1 от плоскости противоположной ей грани.
                // Кроме последней вершины, так как одна из барицентрических координат определяется по другим.
                for (unsigned i = 0; i < N - 1; ++i)
                {
                        // Перпендикуляр от точки к грани — это перпендикуляр к пространству,
                        // образуемому перпендикуляром к симплексу и пространством грани
                        std::swap(simplex_normal, vectors[i]);
                        m_planes[i].n = ortho_nn(vectors);
                        std::swap(simplex_normal, vectors[i]);

                        // Уравнение плоскости
                        // dot(p - org, normal) = dot(p, normal) - dot(org, normal) = dot(p, normal) - d
                        // Плоскость проходит через какую-нибудь вершину грани, например vertices[N - 1].
                        m_planes[i].d = dot(vertices[N - 1], m_planes[i].n);

                        // Относительное расстояние от вершины до плоскости должно быть равно 1
                        T distance = dot(vertices[i], m_planes[i].n) - m_planes[i].d;
                        m_planes[i].n /= distance;
                        m_planes[i].d /= distance;
                }
        }

        T barycentric_coordinate(const Vector<N, T>& point, unsigned i) const
        {
                ASSERT(i < N - 1);
                // Относительное расстояние от грани до точки является координатой точки
                return dot(point, m_planes[i].n) - m_planes[i].d;
        }

        Vector<N, T> barycentric_coordinates(const Vector<N, T>& point) const
        {
                Vector<N, T> coords;
                coords[N - 1] = 1;
                for (unsigned i = 0; i < N - 1; ++i)
                {
                        coords[i] = barycentric_coordinate(point, i);
                        coords[N - 1] -= coords[i];
                }
                return coords;
        }

        bool intersect(const Ray<N, T>& ray, const Vector<N, T>& any_vertex, const Vector<N, T>& normal, T* t) const
        {
                namespace Impl = SimplexGeometryImplementation;

                if (!Impl::plane_intersect(ray, any_vertex, normal, t))
                {
                        return false;
                }

                Vector<N, T> intersection_point = ray.point(*t);

                Vector<N - 1, T> coordinates;

                for (unsigned i = 0; i < N - 1; ++i)
                {
                        coordinates[i] = barycentric_coordinate(intersection_point, i);
                        if (coordinates[i] <= 0 || coordinates[i] >= 1)
                        {
                                return false;
                        }
                }

                return last_coordinate(coordinates) > 0;
        }

        template <typename InterpolationType>
        InterpolationType interpolate(const Vector<N, T>& point, const std::array<InterpolationType, N>& n) const
        {
                Vector<N, T> bc = barycentric_coordinates(point);

                InterpolationType result = bc[0] * n[0];

                for (unsigned i = 1; i < N; ++i)
                {
                        result += bc[i] * n[i];
                }

                return result;
        }
};

template <size_t N, typename T>
class ParallelotopeGeometry
{
        SimplexGeometry<N, T> m_simplex_geometry;

public:
        void set_data(const Vector<N, T>& normal, const Vector<N, T>& org, const std::array<Vector<N, T>, N - 1>& edges)
        {
                std::array<Vector<N, T>, N> points;
                for (unsigned i = 0; i < N - 1; ++i)
                {
                        points[i] = org + edges[i];
                }
                // Начало использовать как последнюю точку, для которой не надо определять координату
                points[N - 1] = org;
                m_simplex_geometry.set_data(normal, points);
        }

        bool intersect(const Ray<N, T>& ray, const Vector<N, T>& any_vertex, const Vector<N, T>& normal, T* t) const
        {
                namespace Impl = SimplexGeometryImplementation;

                if (!Impl::plane_intersect(ray, any_vertex, normal, t))
                {
                        return false;
                }

                Vector<N, T> intersection_point = ray.point(*t);

                for (unsigned i = 0; i < N - 1; ++i)
                {
                        T d = m_simplex_geometry.barycentric_coordinate(intersection_point, i);
                        if (d <= 0 || d >= 1)
                        {
                                return false;
                        }
                }

                return true;
        }
};
