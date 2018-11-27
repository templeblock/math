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

layout(location = 0) in ivec2 window_coordinates;
layout(location = 1) in vec2 texture_coordinates;

uniform mat4 matrix;

out vec2 vs_texture_coordinates;

void main(void)
{
        gl_Position = matrix * vec4(window_coordinates, 0, 1);
        vs_texture_coordinates = texture_coordinates;
}