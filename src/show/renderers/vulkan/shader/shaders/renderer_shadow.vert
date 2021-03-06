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

layout(location = 0) in vec3 position;

layout(std140, set = 0, binding = 0) uniform Matrices
{
        mat4 matrix;
}
matrices;

out gl_PerVertex
{
        vec4 gl_Position;
};

void main(void)
{
        gl_Position = matrices.matrix * vec4(position, 1.0);
}
