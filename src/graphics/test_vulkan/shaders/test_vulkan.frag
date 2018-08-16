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

#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject0
{
        float value_r;
        float value_g;
}
ubo0;

layout(binding = 1) uniform UniformBufferObject1
{
        float value_b;
}
ubo1;

layout(location = 0) in vec3 fragment_color;

layout(location = 0) out vec4 out_color;

void main()
{
        out_color = vec4(fragment_color.r * ubo0.value_r, fragment_color.g * ubo0.value_g, fragment_color.b * ubo1.value_b, 1.0);
}