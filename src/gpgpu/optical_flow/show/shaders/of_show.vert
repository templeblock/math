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

uniform mat4 matrix;

layout(std430, binding = 0) readonly buffer StorageBufferPoints
{
        ivec2 points[];
};
layout(std430, binding = 1) readonly buffer StorageBufferPointsFlow
{
        vec2 points_optical_flow[];
};

void main(void)
{
        // Две вершины на одну точку.
        // Одна вершина начало линии, вторая вершина конец линии.

        uint point_number = gl_VertexID >> 1;

        vec2 s;
        if ((gl_VertexID & 1) == 0)
        {
                s = points[point_number];
        }
        else
        {
                s = round(points[point_number] + points_optical_flow[point_number]);
        }

        gl_Position = matrix * vec4(s + 0.5, 0, 1);
}