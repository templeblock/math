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

#pragma once

#include "com/font/vertices.h"

#include <vector>
#include <vulkan/vulkan.h>

namespace vulkan_text_implementation
{
using Vertex = TextVertex;

std::vector<VkVertexInputBindingDescription> vertex_binding_descriptions();
std::vector<VkVertexInputAttributeDescription> vertex_attribute_descriptions();
}
