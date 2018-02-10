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

#include "image.h"

#include "com/color/colors.h"
#include "com/error.h"
#include "com/file/file.h"
#include "com/file/file_sys.h"
#include "com/interpolation.h"
#include "com/string/str.h"

#include <SFML/Graphics/Image.hpp>
#include <algorithm>

namespace
{
std::string file_name_with_extension(const std::string& file_name, const char* extension)
{
        std::string ext = to_lower(trim(get_extension(file_name)));
        if (ext.size() > 0)
        {
                if (ext != to_lower(trim(extension)))
                {
                        error("Unsupported image file format");
                }
                return file_name;
        }
        return file_name + "." + extension;
}

template <size_t N>
long long mul(const std::array<int, N>& size)
{
        static_assert(N >= 1);
        long long res = size[0];
        for (size_t i = 1; i < N; ++i)
        {
                res *= size[i];
        }
        return res;
}
}

template <size_t N>
Image<N>::Image(const std::array<int, N>& size)
{
        resize(size);
}

template <size_t N>
Image<N>::Image(const std::array<int, N>& size, const std::vector<unsigned char>& srgba_pixels)
{
        if (4ull * mul(size) != srgba_pixels.size())
        {
                error("Image size error for sRGBA pixels");
        }

        read_from_srgba_pixels(size, srgba_pixels.data());
}

template <size_t N>
void Image<N>::resize(const std::array<int, N>& size)
{
        if (m_size == size)
        {
                return;
        }

        for (int v : size)
        {
                if (v < 2)
                {
                        error("Image size is less than 2");
                }
        }

        m_data.clear();
        m_data.shrink_to_fit();

        m_size = size;

        for (unsigned i = 0; i < N; ++i)
        {
                m_max[i] = m_size[i] - 1;
                m_max_0[i] = m_size[i] - 2;
        }

        // Смещения для каждого измерения для перехода к следующей координате по этому измерению.
        // Для x == 1, для y == width, для z == height * width и т.д.
        m_strides[0] = 1;
        for (unsigned i = 1; i < N; ++i)
        {
                m_strides[i] = m_size[i - 1] * m_strides[i - 1];
        }

        // Смещения для следующих элементов от заданного с сортировкой по возрастанию
        // от измерения с максимальным номером к измерению с минимальным номером.
        // Пример для двух измерений:
        // (x    , y    ) = 0
        // (x + 1, y    ) = 1
        // (x    , y + 1) = width
        // (x + 1, y + 1) = width + 1
        for (unsigned i = 0; i < (1 << N); ++i)
        {
                long long offset_index = 0;
                for (unsigned n = 0; n < N; ++n)
                {
                        if ((1 << n) & i)
                        {
                                offset_index += m_strides[n];
                        }
                }
                m_pixel_offsets[i] = offset_index;
        }

        m_data.resize(m_strides[N - 1] * m_size[N - 1]);
}

template <size_t N>
bool Image<N>::empty() const
{
        return m_data.size() == 0;
}

template <size_t N>
void Image<N>::clear(const vec3& color)
{
        std::fill(m_data.begin(), m_data.end(), color);
}

template <size_t N>
long long Image<N>::pixel_index(const std::array<int, N>& p) const
{
        long long index = p[0]; // p[0] * m_strides[0] == p[0]
        for (unsigned i = 1; i < N; ++i)
        {
                index += m_strides[i] * p[i];
        }
        return index;
}

template <size_t N>
template <typename T>
vec3 Image<N>::texture(const Vector<N, T>& p) const
{
        std::array<int, N> x0;
        std::array<T, N> local_x;

        for (unsigned i = 0; i < N; ++i)
        {
                T x = std::clamp(p[i], static_cast<T>(0), static_cast<T>(1)) * m_max[i];
                x0[i] = static_cast<int>(x);
                // Если значение x[i] равно максимуму (это целое число), то x0[i] получится
                // неправильным, поэтому требуется корректировка для этого случая
                x0[i] = std::min(x0[i], m_max_0[i]);
                local_x[i] = x - x0[i];
        }

        long long index = pixel_index(x0);

        std::array<vec3, (1 << N)> pixels;
        pixels[0] = m_data[index]; // index + m_pixel_offsets[0] == index
        for (unsigned i = 1; i < pixels.size(); ++i)
        {
                pixels[i] = m_data[index + m_pixel_offsets[i]];
        }

        return interpolation(pixels, local_x);
}

template <size_t N>
void Image<N>::read_from_srgba_pixels(const std::array<int, N>& size, const unsigned char* srgba_pixels)
{
        resize(size);

        for (size_t i = 0, p = 0; i < m_data.size(); p += 4, ++i)
        {
                m_data[i] = srgb_integer_to_rgb_float(srgba_pixels[p], srgba_pixels[p + 1], srgba_pixels[p + 2]);
        }
}

template <size_t N>
template <size_t X>
std::enable_if_t<X == 2> Image<N>::read_from_file(const std::string& file_name)
{
        static_assert(N == X);

        sf::Image image;

        if (!image.loadFromFile(file_name))
        {
                error("Error read image from file " + file_name);
        }

        std::array<int, N> size;
        size[0] = image.getSize().x;
        size[1] = image.getSize().y;
        read_from_srgba_pixels(size, image.getPixelsPtr());
}

// Запись в формат PPM с цветом sRGB
template <size_t N>
template <size_t X>
std::enable_if_t<X == 2> Image<N>::write_to_file(const std::string& file_name) const
{
        static_assert(N == X);

        if (empty())
        {
                error("No data to write the image to the file " + file_name);
        }

        CFile fp(file_name_with_extension(file_name, "ppm"), "wb");

        long long width = m_size[0];
        long long height = m_size[1];
        if (fprintf(fp, "P6\n%lld %lld\n255\n", width, height) <= 0)
        {
                error("Error writing image header");
        }

        std::vector<std::array<unsigned char, 3>> buffer(m_data.size());

        for (size_t i = 0; i < m_data.size(); ++i)
        {
                buffer[i] = rgb_float_to_srgb_integer(m_data[i]);
        }

        if (fwrite(buffer.data(), 3, buffer.size(), fp) != buffer.size())
        {
                error("Error writing image data");
        }
}

// Текстурные координаты могут отсчитываться снизу, поэтому нужна эта функция
template <size_t N>
template <size_t X>
std::enable_if_t<X == 2> Image<N>::flip_vertically()
{
        static_assert(N == X);

        std::vector<vec3> tmp = m_data;

        long long width = m_size[0];
        long long height = m_size[1];
        for (int y = 0; y < height; ++y)
        {
                for (int x = 0; x < width; ++x)
                {
                        int index_from = y * width + x;
                        int index_to = (height - y - 1) * width + x;
                        m_data[index_to] = tmp[index_from];
                }
        }
}

template class Image<2>;
template class Image<3>;
template class Image<4>;

template vec3 Image<2>::texture(const Vector<2, double>& p) const;
template vec3 Image<3>::texture(const Vector<3, double>& p) const;
template vec3 Image<4>::texture(const Vector<4, double>& p) const;

template void Image<2>::flip_vertically();
template void Image<2>::read_from_file(const std::string& file_name);
template void Image<2>::write_to_file(const std::string& file_name) const;
