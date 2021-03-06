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

#version 450

#if !defined(VULKAN)
#extension GL_ARB_bindless_texture : require
#endif

float rgb_to_srgb(float c)
{
        if (c >= 1.0)
        {
                return 1.0;
        }
        if (c >= 0.0031308)
        {
                return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
        }
        if (c > 0.0)
        {
                return c * 12.92;
        }
        return 0.0;
}

float srgb_to_rgb(float c)
{
        if (c >= 1.0)
        {
                return 1.0;
        }
        if (c >= 0.04045)
        {
                return pow((c + 0.055) / 1.055, 2.4);
        }
        if (c > 0.0)
        {
                return c / 12.92;
        }
        return 0.0;
}

vec4 rgb_to_srgb(vec4 c)
{
        return vec4(rgb_to_srgb(c.r), rgb_to_srgb(c.g), rgb_to_srgb(c.b), c.a);
}

vec4 srgb_to_rgb(vec4 c)
{
        return vec4(srgb_to_rgb(c.r), srgb_to_rgb(c.g), srgb_to_rgb(c.b), c.a);
}

vec3 rgb_to_srgb(vec3 c)
{
        return vec3(rgb_to_srgb(c.r), rgb_to_srgb(c.g), rgb_to_srgb(c.b));
}

vec3 srgb_to_rgb(vec3 c)
{
        return vec3(srgb_to_rgb(c.r), srgb_to_rgb(c.g), srgb_to_rgb(c.b));
}

float luminance_of_rgb(vec3 c)
{
        return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

float luminance_of_rgb(vec4 c)
{
        return luminance_of_rgb(c.rgb);
}

vec2 complex_mul(vec2 a, vec2 b)
{
        return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}
dvec2 complex_mul(dvec2 a, dvec2 b)
{
        return dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

uint bit_reverse(uint i, uint bits)
{
        return bitfieldReverse(i) >> (32 - bits);
}

// Для DFT
#define complex vec2
#define float_point float
const float_point PI = 3.1415926535897932384626433832795028841971693993751;
