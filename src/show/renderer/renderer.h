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

#include "com/mat.h"
#include "com/vec.h"
#include "graphics/objects.h"
#include "obj/obj.h"

#include <memory>

struct IRenderer
{
        virtual ~IRenderer() = default;

        virtual void set_light_a(const vec3& light) = 0;
        virtual void set_light_d(const vec3& light) = 0;
        virtual void set_light_s(const vec3& light) = 0;
        virtual void set_default_color(const vec3& color) = 0;
        virtual void set_wireframe_color(const vec3& color) = 0;
        virtual void set_default_ns(double default_ns) = 0;
        virtual void set_show_smooth(bool show) = 0;
        virtual void set_show_wireframe(bool show) = 0;
        virtual void set_show_shadow(bool show) = 0;
        virtual void set_show_materials(bool show) = 0;

        virtual void set_shadow_zoom(double) = 0;

        virtual void set_matrices(const mat4& shadow_matrix, const mat4& main_matrix) = 0;

        virtual void set_light_direction(vec3 dir) = 0;
        virtual void set_camera_direction(vec3 dir) = 0;

        virtual void draw(bool draw_to_buffer) = 0;

        virtual void free_buffers() = 0;
        virtual void set_size(int width, int height) = 0;

        virtual const TextureRGBA32F& get_color_buffer_texture() const = 0;
        virtual const TextureR32I& get_object_texture() const = 0;

        virtual void add_object(const IObj* obj, double size, const vec3& position, int id, int scale_id) = 0;
        virtual void delete_object(int id) = 0;
        virtual void show_object(int id) = 0;
        virtual void delete_all() = 0;
};

std::unique_ptr<IRenderer> create_renderer();