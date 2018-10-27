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

#include "shader.h"

#include "com/font/vertices.h"

namespace
{
template <typename T>
void copy_to_buffer(const vulkan::UniformBufferWithHostVisibleMemory& buffer, VkDeviceSize offset, const T& data)
{
        buffer.copy(offset, &data, sizeof(data));
}
}

namespace vulkan_text_shaders
{
static_assert(sizeof(TextVertex) == sizeof(Vector<2, int32_t>) + sizeof(Vector<2, float>));
static_assert(std::is_same_v<decltype(TextVertex::v), Vector<2, int32_t>>);
static_assert(std::is_same_v<decltype(TextVertex::t), Vector<2, float>>);

std::vector<VkVertexInputBindingDescription> vertex_binding_descriptions()
{
        std::vector<VkVertexInputBindingDescription> descriptions;

        {
                VkVertexInputBindingDescription d = {};
                d.binding = 0;
                d.stride = sizeof(TextVertex);
                d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                descriptions.push_back(d);
        }

        return descriptions;
}

std::vector<VkVertexInputAttributeDescription> vertex_attribute_descriptions()
{
        std::vector<VkVertexInputAttributeDescription> descriptions;

        {
                VkVertexInputAttributeDescription d = {};
                d.binding = 0;
                d.location = 0;
                d.format = VK_FORMAT_R32G32_SINT;
                d.offset = offsetof(TextVertex, v);

                descriptions.push_back(d);
        }
        {
                VkVertexInputAttributeDescription d = {};
                d.binding = 0;
                d.location = 1;
                d.format = VK_FORMAT_R32G32_SFLOAT;
                d.offset = offsetof(TextVertex, t);

                descriptions.push_back(d);
        }

        return descriptions;
}

std::vector<VkDescriptorSetLayoutBinding> TextMemory::descriptor_set_layout_bindings()
{
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        {
                VkDescriptorSetLayoutBinding b = {};
                b.binding = 0;
                b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                b.descriptorCount = 1;
                b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                bindings.push_back(b);
        }
        {
                VkDescriptorSetLayoutBinding b = {};
                b.binding = 1;
                b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                b.descriptorCount = 1;
                b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

                bindings.push_back(b);
        }
        {
                VkDescriptorSetLayoutBinding b = {};
                b.binding = 2;
                b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                b.descriptorCount = 1;
                b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

                bindings.push_back(b);
        }

        return bindings;
}

TextMemory::TextMemory(const vulkan::Device& device, VkSampler sampler, const vulkan::GrayscaleTexture* texture)
        : m_descriptor_set_layout(vulkan::create_descriptor_set_layout(device, descriptor_set_layout_bindings())),
          m_descriptors(vulkan::Descriptors(device, 1, m_descriptor_set_layout, descriptor_set_layout_bindings()))
{
        std::vector<Variant<VkDescriptorBufferInfo, VkDescriptorImageInfo>> infos;
        std::vector<uint32_t> bindings;

        {
                m_uniform_buffers.emplace_back(device, sizeof(Matrices));
                m_matrices_buffer_index = m_uniform_buffers.size() - 1;

                VkDescriptorBufferInfo buffer_info = {};
                buffer_info.buffer = m_uniform_buffers.back();
                buffer_info.offset = 0;
                buffer_info.range = m_uniform_buffers.back().size();

                infos.push_back(buffer_info);

                bindings.push_back(0);
        }
        {
                VkDescriptorImageInfo image_info = {};
                image_info.imageLayout = texture->image_layout();
                image_info.imageView = texture->image_view();
                image_info.sampler = sampler;

                infos.push_back(image_info);

                bindings.push_back(1);
        }
        {
                m_uniform_buffers.emplace_back(device, sizeof(Drawing));
                m_drawing_buffer_index = m_uniform_buffers.size() - 1;

                VkDescriptorBufferInfo buffer_info = {};
                buffer_info.buffer = m_uniform_buffers.back();
                buffer_info.offset = 0;
                buffer_info.range = m_uniform_buffers.back().size();

                infos.push_back(buffer_info);

                bindings.push_back(2);
        }

        m_descriptor_set = m_descriptors.create_and_update_descriptor_set(bindings, infos);
}

VkDescriptorSetLayout TextMemory::descriptor_set_layout() const noexcept
{
        return m_descriptor_set_layout;
}

VkDescriptorSet TextMemory::descriptor_set() const noexcept
{
        return m_descriptor_set;
}

template <typename T>
void TextMemory::copy_to_matrices_buffer(VkDeviceSize offset, const T& data) const
{
        copy_to_buffer(m_uniform_buffers[m_matrices_buffer_index], offset, data);
}
template <typename T>
void TextMemory::copy_to_drawing_buffer(VkDeviceSize offset, const T& data) const
{
        copy_to_buffer(m_uniform_buffers[m_drawing_buffer_index], offset, data);
}

void TextMemory::set_matrix(const mat4& matrix) const
{
        decltype(Matrices().matrix) m = transpose(to_matrix<float>(matrix));
        copy_to_matrices_buffer(offsetof(Matrices, matrix), m);
}

void TextMemory::set_color(const Color& color) const
{
        decltype(Drawing().color) c = color.to_rgb_vector<float>();
        copy_to_drawing_buffer(offsetof(Drawing, color), c);
}

}
