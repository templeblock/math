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

#include "of_show.h"

#include "com/conversion.h"
#include "com/error.h"
#include "com/time.h"
#include "gpu_2d/opengl/optical_flow/compute/of_gl2d.h"
#include "graphics/opengl/objects.h"

#include <limits>
#include <vector>

// clang-format off
constexpr const char vertex_shader[]
{
#include "of.vert.str"
};
constexpr const char fragment_shader[]
{
#include "of.frag.str"
};
constexpr const char vertex_debug_shader[]
{
#include "of_debug.vert.str"
};
constexpr const char fragment_debug_shader[]
{
#include "of_debug.frag.str"
};
// clang-format on

// Расстояние между точками потока на экране в миллиметрах
constexpr double DISTANCE_BETWEEN_POINTS = 2;

// Интервал ожидания для расчёта потока не для каждого кадра
// constexpr double COMPUTE_INTERVAL_SECONDS = 1.0 / 10;

constexpr int SIZE_OF_VEC2 = 2 * 4; // GLSL vec2

namespace
{
void create_points_for_top_level(int width, int height, int distance, int* point_count_x, int* point_count_y,
                                 std::vector<vec2i>* points)
{
        int size = distance + 1;
        *point_count_x = (width - 2 * distance + size - 1) / size;
        *point_count_y = (height - 2 * distance + size - 1) / size;

        int point_count = *point_count_x * *point_count_y;

        points->clear();
        points->resize(point_count);

        int index = 0;
        for (int y = distance; y < height - distance; y += size)
        {
                for (int x = distance; x < width - distance; x += size)
                {
                        (*points)[index++] = vec2i(x, y);
                }
        }

        ASSERT(index == point_count);
}
}

class OpticalFlow::Impl final
{
        const int m_width, m_height;
        opengl::GraphicsProgram m_draw_prog;
        opengl::GraphicsProgram m_draw_prog_debug;

        opengl::TextureRGBA32F m_source_image;

        opengl::ShaderStorageBuffer m_top_points;
        opengl::ShaderStorageBuffer m_top_points_flow;
        int m_top_point_count_x;
        int m_top_point_count_y;

        bool m_flow_computed = false;
        double m_last_time = std::numeric_limits<double>::lowest();

        std::unique_ptr<OpticalFlowGL2D> m_optical_flow;

        void draw_flow_lines()
        {
                m_top_points.bind(0);
                m_top_points_flow.bind(1);

                m_draw_prog.draw_arrays(GL_POINTS, 0, m_top_point_count_x * m_top_point_count_y * 2);
                m_draw_prog.draw_arrays(GL_LINES, 0, m_top_point_count_x * m_top_point_count_y * 2);
        }

public:
        Impl(int width, int height, double window_ppi, const mat4& matrix)
                : m_width(width),
                  m_height(height),
                  m_draw_prog(opengl::VertexShader(vertex_shader), opengl::FragmentShader(fragment_shader)),
                  m_draw_prog_debug(opengl::VertexShader(vertex_debug_shader), opengl::FragmentShader(fragment_debug_shader)),
                  m_source_image(m_width, m_height)
        {
                std::vector<vec2i> top_points;
                create_points_for_top_level(m_width, m_height, millimeters_to_pixels(DISTANCE_BETWEEN_POINTS, window_ppi),
                                            &m_top_point_count_x, &m_top_point_count_y, &top_points);

                m_top_points.load_dynamic_copy(top_points);
                m_top_points_flow.create_dynamic_copy(top_points.size() * SIZE_OF_VEC2);

                m_draw_prog.set_uniform_float("matrix", matrix);

                m_optical_flow = create_optical_flow_gl2d(width, height, m_source_image, m_top_point_count_x, m_top_point_count_y,
                                                          m_top_points, m_top_points_flow);
        }

        void reset()
        {
                m_last_time = std::numeric_limits<double>::lowest();
                m_flow_computed = false;
                m_optical_flow->reset();
        }

        void take_image_from_framebuffer()
        {
                m_source_image.copy_texture_sub_image();
        }

        void draw()
        {
                opengl::GLEnableAndRestore<GL_SCISSOR_TEST> e;
                glScissor(0, 0, m_width, m_height);

#if 0
                double current_time = time_in_seconds();
                if (current_time - m_last_time < COMPUTE_INTERVAL_SECONDS)
                {
                        if (m_flow_computed)
                        {
                                draw_flow_lines();
                        }
                        return;
                }
                m_last_time = current_time;
#endif

                if (!m_optical_flow->exec())
                {
                        return;
                }

#if 0
                // m_draw_prog_debug.set_uniform_handle("tex", m_optical_flow->image_pyramid_dx_texture());
                m_draw_prog_debug.set_uniform_handle("tex", m_optical_flow->image_pyramid_texture());
                m_draw_prog_debug.draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
#else
                draw_flow_lines();
                m_flow_computed = true;
#endif
        }
};

OpticalFlow::OpticalFlow(int width, int height, double window_ppi, const mat4& matrix)
        : m_impl(std::make_unique<Impl>(width, height, window_ppi, matrix))
{
}

OpticalFlow::~OpticalFlow() = default;

void OpticalFlow::reset()
{
        m_impl->reset();
}

void OpticalFlow::take_image_from_framebuffer()
{
        m_impl->take_image_from_framebuffer();
}

void OpticalFlow::draw()
{
        m_impl->draw();
}