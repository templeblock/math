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

#include "base.h"

#include "com/vec.h"
#include "path_tracing/objects.h"
#include "path_tracing/ray.h"

class TableTriangle final : public GeometricTriangle
{
        const vec3* m_vertices;
        const vec3* m_normals;
        const vec2* m_texcoords;

        int m_v0, m_v1, m_v2;
        int m_n0, m_n1, m_n2;
        int m_t0, m_t1, m_t2;
        int m_material;

        vec3 m_normal;
        vec3 m_u_beta, m_u_gamma;

        enum class NormalType : char
        {
                NO_NORMALS,
                USE_NORMALS,
                NEGATE_NORMALS
        } m_normal_type;
        bool m_negate_normal_0, m_negate_normal_1, m_negate_normal_2;

public:
        TableTriangle(const vec3* points, const vec3* normals, const vec2* texcoords, int v1, int v2, int v3, bool has_normals,
                      int n1, int n2, int n3, bool has_texcoords, int t1, int t2, int t3, int material);

        int get_material() const;

        bool has_texcoord() const;
        vec2 texcoord(const vec3& point) const;

        bool intersect(const ray3& r, double* t) const override;

        vec3 geometric_normal() const;
        vec3 shading_normal(const vec3& point) const;

        const vec3& v0() const override;
        const vec3& v1() const override;
        const vec3& v2() const override;
};