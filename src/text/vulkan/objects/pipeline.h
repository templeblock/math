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

#include "graphics/vulkan/objects.h"
#include "graphics/vulkan/shader.h"

#include <optional>
#include <vector>

namespace vulkan_text_implementation
{
struct TextPipelineCreateInfo
{
        std::optional<VkDevice> device;
        std::optional<VkRenderPass> render_pass;
        std::optional<VkPipelineLayout> pipeline_layout;
        std::optional<uint32_t> width;
        std::optional<uint32_t> height;
        std::optional<const std::vector<const vulkan::Shader*>*> shaders;
        std::optional<const std::vector<VkVertexInputBindingDescription>*> binding_descriptions;
        std::optional<const std::vector<VkVertexInputAttributeDescription>*> attribute_descriptions;
};

vulkan::Pipeline create_text_pipeline(const TextPipelineCreateInfo& info);
}
