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
По книге

Aaftab Munshi, Benedict R. Gaster, Timothy G. Mattson, James Fung, Dan Ginsburg.
OpenCL Programming Guide.
Addison-Wesley, 2011.
Chapter 19. Optical Flow.

Дополнительная информация

Salil Kapur, Nisarg Thakkar.
Mastering OpenCV Android Application Programming.
Packt Publishing, 2015.
Chapter 5. Tracking Objects in Videos.
*/

#include "of_gl2d.h"

#include "com/error.h"
#include "com/log.h"
#include "com/math.h"
#include "com/print.h"

#include <array>

// clang-format off
constexpr const char sobel_compute_shader[]
{
#include "of_sobel.comp.str"
};
constexpr const char flow_compute_shader[]
{
#include "of_flow.comp.str"
};
constexpr const char downsample_compute_shader[]
{
#include "of_downsample.comp.str"
};
constexpr const char grayscale_compute_shader[]
{
#include "of_grayscale.comp.str"
};
// clang-format on

// Размер по X и по Y группы потоков вычислительных шейдеров
constexpr int GROUP_SIZE = 16;
// Минимальный размер изображения для пирамиды изображений
constexpr int BOTTOM_IMAGE_SIZE = 16;

// Параметры алгоритма для передачи в вычислительный шейдер
// Радиус окрестности точки
constexpr int RADIUS = 6;
// Максимальное количество итераций
constexpr int ITERATION_COUNT = 10;
// Если на итерации квадрат потока меньше этого значения, то выход из цикла
constexpr float STOP_MOVE_SQUARE = square(1e-3f);
// Если определитель матрицы G меньше этого значения, то считается, что нет потока
constexpr float MIN_DETERMINANT = 1;

namespace
{
void create_image_pyramid_sizes(int width, int height, int min, std::vector<vec2i>* level_dimensions)
{
        level_dimensions->clear();
        level_dimensions->emplace_back(width, height);
        while (true)
        {
                int new_width = (width + 1) / 2;
                int new_height = (height + 1) / 2;

                if (new_width < min)
                {
                        new_width = width;
                }
                if (new_height < min)
                {
                        new_height = height;
                }

                if (new_width == width && new_height == height)
                {
                        break;
                }

                level_dimensions->emplace_back(new_width, new_height);

                width = new_width;
                height = new_height;
        }

#if 0
        for (const vec2i& v : *level_dimensions)
        {
                LOG(to_string(v[0]) + " x " + to_string(v[1]));
        }
#endif
}

class ImageR32F
{
        opengl::TextureR32F m_texture;
        GLuint64 m_image_write_handle;
        GLuint64 m_image_read_handle;
        GLuint64 m_texture_handle;
        int m_width;
        int m_height;

public:
        ImageR32F(int x, int y)
                : m_texture(x, y),
                  m_image_write_handle(m_texture.image_resident_handle_write_only()),
                  m_image_read_handle(m_texture.image_resident_handle_read_only()),
                  m_texture_handle(m_texture.texture().texture_resident_handle()),
                  m_width(x),
                  m_height(y)
        {
        }
        int width() const
        {
                return m_width;
        }
        int height() const
        {
                return m_height;
        }
        GLuint64 image_write_handle() const
        {
                return m_image_write_handle;
        }
        GLuint64 image_read_handle() const
        {
                return m_image_read_handle;
        }
        GLuint64 texture_handle() const
        {
                return m_texture_handle;
        }
};

void create_textures(const std::vector<vec2i>& level_dimensions, std::vector<ImageR32F>* textures)
{
        textures->clear();
        for (const vec2i& d : level_dimensions)
        {
                textures->emplace_back(d[0], d[1]);
        }
}

void create_flow_buffers(const std::vector<vec2i>& level_dimensions, std::vector<opengl::ShaderStorageBuffer>* buffers)
{
        buffers->clear();
        buffers->resize(level_dimensions.size());
        for (unsigned i = 0; i < level_dimensions.size(); ++i)
        {
                (*buffers)[i].create_dynamic_copy(level_dimensions[i][0] * level_dimensions[i][1] * sizeof(vec2f));
        }
}

class Impl final : public OpticalFlowGL2D
{
        const int m_width, m_height;
        const int m_groups_x, m_groups_y;

        int m_top_point_count_x;
        int m_top_point_count_y;

        const opengl::ShaderStorageBuffer& m_top_points;
        const opengl::ShaderStorageBuffer& m_top_points_flow;

        opengl::ComputeProgram m_comp_sobel;
        opengl::ComputeProgram m_comp_flow;
        opengl::ComputeProgram m_comp_downsample;
        opengl::ComputeProgram m_comp_grayscale;

        std::array<std::vector<ImageR32F>, 2> m_image_pyramid;
        std::vector<ImageR32F> m_image_pyramid_dx;
        std::vector<ImageR32F> m_image_pyramid_dy;

        std::vector<opengl::ShaderStorageBuffer> m_image_pyramid_flow;
        int m_i_index = 0;
        int m_j_index = 1;
        bool m_image_I_exists = false;

        void build_image_pyramid(std::vector<ImageR32F>* pyramid)
        {
                // уровень 0 заполняется по исходному изображению
                m_comp_grayscale.set_uniform_handle("img_dst", (*pyramid)[0].image_write_handle());
                m_comp_grayscale.dispatch_compute(m_groups_x, m_groups_y, 1, GROUP_SIZE, GROUP_SIZE, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

                // Каждый следующий уровень меньше предыдущего
                for (unsigned i = 1; i < pyramid->size(); ++i)
                {
                        const ImageR32F& img_big = (*pyramid)[i - 1];
                        const ImageR32F& img_small = (*pyramid)[i];

                        int k_x = (img_small.width() != img_big.width()) ? 2 : 1;
                        int k_y = (img_small.height() != img_big.height()) ? 2 : 1;

                        ASSERT(k_x > 1 || k_y > 1);

                        m_comp_downsample.set_uniform_handle("img_big", img_big.image_read_handle());
                        m_comp_downsample.set_uniform_handle("img_small", img_small.image_write_handle());
                        m_comp_downsample.set_uniform("k_x", k_x);
                        m_comp_downsample.set_uniform("k_y", k_y);

                        int groups_x = group_count(img_small.width(), GROUP_SIZE);
                        int groups_y = group_count(img_small.height(), GROUP_SIZE);

                        m_comp_downsample.dispatch_compute(groups_x, groups_y, 1, GROUP_SIZE, GROUP_SIZE, 1);
                        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                }
        }

        void compute_dxdy(const std::vector<ImageR32F>& image_pyramid, const std::vector<ImageR32F>& image_pyramid_dx,
                          const std::vector<ImageR32F>& image_pyramid_dy)
        {
                ASSERT(image_pyramid.size() == image_pyramid_dx.size() && image_pyramid.size() == image_pyramid_dy.size());

                for (unsigned i = 0; i < image_pyramid.size(); ++i)
                {
                        m_comp_sobel.set_uniform_handle("img_I", image_pyramid[i].image_read_handle());
                        m_comp_sobel.set_uniform_handle("img_dx", image_pyramid_dx[i].image_write_handle());
                        m_comp_sobel.set_uniform_handle("img_dy", image_pyramid_dy[i].image_write_handle());

                        int groups_x = group_count(image_pyramid[i].width(), GROUP_SIZE);
                        int groups_y = group_count(image_pyramid[i].height(), GROUP_SIZE);

                        m_comp_sobel.dispatch_compute(groups_x, groups_y, 1, GROUP_SIZE, GROUP_SIZE, 1);
                }

                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }

        void compute_optical_flow(const std::vector<ImageR32F>& image_pyramid_I, const std::vector<ImageR32F>& image_pyramid_dx,
                                  const std::vector<ImageR32F>& image_pyramid_dy,
                                  const std::vector<opengl::ShaderStorageBuffer>& image_pyramid_flow,
                                  const std::vector<ImageR32F>& image_pyramid_J)
        {
                int image_pyramid_I_size = image_pyramid_I.size();

                for (int i = image_pyramid_I_size - 1; i >= 0; --i)
                {
                        int points_x, points_y;
                        if (i != 0)
                        {
                                // Если не самый верхний уровень, то расчёт для всех точек

                                m_comp_flow.set_uniform("all_points", 1);

                                image_pyramid_flow[i].bind(1);

                                points_x = image_pyramid_I[i].width();
                                points_y = image_pyramid_I[i].height();
                        }
                        else
                        {
                                // Если самый верхний уровень, то расчёт только для заданных точек для рисования на экране

                                m_comp_flow.set_uniform("all_points", 0);

                                m_top_points.bind(0);
                                m_top_points_flow.bind(1);

                                points_x = m_top_point_count_x;
                                points_y = m_top_point_count_y;
                        }

                        if (i != image_pyramid_I_size - 1)
                        {
                                // Если не самый нижний уровень, то в качестве приближения использовать поток,
                                // полученный на меньших изображениях

                                m_comp_flow.set_uniform("use_guess", 1);

                                m_comp_flow.set_uniform("guess_width", image_pyramid_I[i + 1].width());

                                image_pyramid_flow[i + 1].bind(2);

                                int kx = (image_pyramid_I[i + 1].width() != image_pyramid_I[i].width()) ? 2 : 1;
                                int ky = (image_pyramid_I[i + 1].height() != image_pyramid_I[i].height()) ? 2 : 1;
                                m_comp_flow.set_uniform("guess_kx", kx);
                                m_comp_flow.set_uniform("guess_ky", ky);
                        }
                        else
                        {
                                // Самый нижний уровень пирамиды, поэтому нет начального потока
                                m_comp_flow.set_uniform("use_guess", 0);
                        }

                        m_comp_flow.set_uniform("point_count_x", points_x);
                        m_comp_flow.set_uniform("point_count_y", points_y);

                        int groups_x = group_count(points_x, GROUP_SIZE);
                        int groups_y = group_count(points_y, GROUP_SIZE);

                        m_comp_flow.set_uniform_handle("img_dx", image_pyramid_dx[i].image_read_handle());
                        m_comp_flow.set_uniform_handle("img_dy", image_pyramid_dy[i].image_read_handle());
                        m_comp_flow.set_uniform_handle("img_I", image_pyramid_I[i].image_read_handle());
                        m_comp_flow.set_uniform_handle("tex_J", image_pyramid_J[i].texture_handle());

                        m_comp_flow.dispatch_compute(groups_x, groups_y, 1, GROUP_SIZE, GROUP_SIZE, 1);

                        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                }
        }

        void reset() override
        {
                m_image_I_exists = false;
        }

        bool exec() override
        {
                // Обозначения: I и i - предыдущее изображение, J и j - следующее изображение

                std::swap(m_i_index, m_j_index);

                build_image_pyramid(&m_image_pyramid[m_j_index]);

                if (!m_image_I_exists)
                {
                        m_image_I_exists = true;
                        return false;
                }

                compute_dxdy(m_image_pyramid[m_i_index], m_image_pyramid_dx, m_image_pyramid_dy);

                compute_optical_flow(m_image_pyramid[m_i_index], m_image_pyramid_dx, m_image_pyramid_dy, m_image_pyramid_flow,
                                     m_image_pyramid[m_j_index]);

                return true;
        }

        GLuint64 image_pyramid_dx_texture() const override
        {
                return m_image_pyramid_dx[0].texture_handle();
        }
        GLuint64 image_pyramid_texture() const override
        {
                return m_image_pyramid[m_i_index][0].texture_handle();
        }

public:
        Impl(int width, int height, const opengl::TextureRGBA32F& source_image, int top_point_count_x, int top_point_count_y,
             const opengl::ShaderStorageBuffer& top_points, const opengl::ShaderStorageBuffer& top_points_flow)
                : m_width(width),
                  m_height(height),
                  m_groups_x(group_count(m_width, GROUP_SIZE)),
                  m_groups_y(group_count(m_height, GROUP_SIZE)),
                  m_top_point_count_x(top_point_count_x),
                  m_top_point_count_y(top_point_count_y),
                  m_top_points(top_points),
                  m_top_points_flow(top_points_flow),
                  m_comp_sobel(opengl::ComputeShader(sobel_compute_shader)),
                  m_comp_flow(opengl::ComputeShader(flow_compute_shader)),
                  m_comp_downsample(opengl::ComputeShader(downsample_compute_shader)),
                  m_comp_grayscale(opengl::ComputeShader(grayscale_compute_shader))
        {
                std::vector<vec2i> level_dimensions;

                create_image_pyramid_sizes(m_width, m_height, BOTTOM_IMAGE_SIZE, &level_dimensions);

                create_textures(level_dimensions, &m_image_pyramid[m_i_index]);
                create_textures(level_dimensions, &m_image_pyramid[m_j_index]);
                create_textures(level_dimensions, &m_image_pyramid_dx);
                create_textures(level_dimensions, &m_image_pyramid_dy);

                create_flow_buffers(level_dimensions, &m_image_pyramid_flow);

                m_comp_grayscale.set_uniform_handle("img_src", source_image.image_resident_handle_read_only());

                m_comp_flow.set_uniform("RADIUS", RADIUS);
                m_comp_flow.set_uniform("ITERATION_COUNT", ITERATION_COUNT);
                m_comp_flow.set_uniform("STOP_MOVE_SQUARE", STOP_MOVE_SQUARE);
                m_comp_flow.set_uniform("MIN_DETERMINANT", MIN_DETERMINANT);
        }
};
}

std::unique_ptr<OpticalFlowGL2D> create_optical_flow_gl2d(int width, int height, const opengl::TextureRGBA32F& source_image,
                                                          int top_point_count_x, int top_point_count_y,
                                                          const opengl::ShaderStorageBuffer& top_points,
                                                          const opengl::ShaderStorageBuffer& top_points_flow)
{
        return std::make_unique<Impl>(width, height, source_image, top_point_count_x, top_point_count_y, top_points,
                                      top_points_flow);
}