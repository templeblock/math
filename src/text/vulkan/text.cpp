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

#include "text.h"

#include "com/container.h"
#include "com/font/font.h"
#include "com/font/glyphs.h"
#include "com/font/vertices.h"
#include "com/log.h"
#include "graphics/vulkan/buffers.h"
#include "graphics/vulkan/create.h"
#include "graphics/vulkan/error.h"
#include "graphics/vulkan/shader.h"
#include "text/vulkan/objects/buffers.h"
#include "text/vulkan/objects/sampler.h"
#include "text/vulkan/shader/memory.h"
#include "text/vulkan/shader/vertex.h"

#include <optional>
#include <thread>

// Это в шейдерах layout(set = N, ...)
constexpr uint32_t TEXT_SET_NUMBER = 0;

constexpr int INDIRECT_BUFFER_COMMAND_COUNT = 1;
constexpr int INDIRECT_BUFFER_COMMAND_NUMBER = 0;

constexpr int VERTEX_BUFFER_FIRST_SIZE = 10;

// clang-format off
constexpr uint32_t vertex_shader[]
{
#include "text.vert.spr"
};
constexpr uint32_t fragment_shader[]
{
#include "text.frag.spr"
};
// clang-format on

namespace impl = vulkan_text_implementation;

namespace
{
class Glyphs
{
        int m_width;
        int m_height;
        std::unordered_map<char32_t, FontGlyph> m_glyphs;
        std::vector<std::uint_least8_t> m_pixels;

public:
        Glyphs(int size, unsigned max_image_dimension)
        {
                Font font(size);
                create_font_glyphs(font, max_image_dimension, max_image_dimension, &m_glyphs, &m_width, &m_height, &m_pixels);
        }
        int width() const noexcept
        {
                return m_width;
        }
        int height() const noexcept
        {
                return m_height;
        }
        std::unordered_map<char32_t, FontGlyph>& glyphs() noexcept
        {
                return m_glyphs;
        }
        std::vector<std::uint_least8_t>& pixels() noexcept
        {
                return m_pixels;
        }
};

class Impl final : public VulkanText
{
        const std::thread::id m_thread_id = std::this_thread::get_id();

        const vulkan::VulkanInstance& m_instance;

        vulkan::Sampler m_sampler;
        vulkan::GrayscaleTexture m_glyph_texture;
        std::unordered_map<char32_t, FontGlyph> m_glyphs;

        impl::TextMemory m_shader_memory;

        vulkan::VertexShader m_text_vert;
        vulkan::FragmentShader m_text_frag;

        vulkan::PipelineLayout m_pipeline_layout;

        std::optional<vulkan::VertexBufferWithHostVisibleMemory> m_vertex_buffer;
        vulkan::IndirectBufferWithHostVisibleMemory m_indirect_buffer;

        std::optional<impl::TextBuffers> m_buffers;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        void set_color(const Color& color) const override
        {
                m_shader_memory.set_color(color);
        }

        void set_matrix(const mat4& matrix) const override
        {
                m_shader_memory.set_matrix(matrix);
        }

        void draw_commands(VkCommandBuffer command_buffer) const
        {
                ASSERT(std::this_thread::get_id() == m_thread_id);

                ASSERT(m_vertex_buffer && m_vertex_buffer->size() > 0);

                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

                VkDescriptorSet descriptor_set = m_shader_memory.descriptor_set();

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, TEXT_SET_NUMBER,
                                        1 /*set count*/, &descriptor_set, 0, nullptr);

                std::array<VkBuffer, 1> buffers = {*m_vertex_buffer};
                std::array<VkDeviceSize, 1> offsets = {0};

                vkCmdBindVertexBuffers(command_buffer, 0, buffers.size(), buffers.data(), offsets.data());

                vkCmdDrawIndirect(command_buffer, m_indirect_buffer, m_indirect_buffer.offset(INDIRECT_BUFFER_COMMAND_NUMBER), 1,
                                  m_indirect_buffer.stride());
        }

        void create_buffers(const vulkan::Swapchain* swapchain, const mat4& matrix) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT(swapchain);

                m_instance.device_wait_idle();

                m_buffers.emplace(*swapchain, m_instance.device(), m_instance.graphics_command_pool());

                m_pipeline =
                        m_buffers->create_pipeline({&m_text_vert, &m_text_frag}, m_pipeline_layout,
                                                   impl::vertex_binding_descriptions(), impl::vertex_attribute_descriptions());

                m_buffers->create_command_buffers(std::bind(&Impl::draw_commands, this, std::placeholders::_1));

                set_matrix(matrix);
        }

        void delete_buffers() override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                if (m_buffers)
                {
                        m_instance.device_wait_idle();

                        m_buffers.reset();
                }
        }

        template <typename T>
        void draw_text(VkFence queue_fence, VkQueue graphics_queue, VkSemaphore wait_semaphore, VkSemaphore finished_semaphore,
                       unsigned image_index, int step_y, int x, int y, const T& text)
        {
                ASSERT(std::this_thread::get_id() == m_thread_id);

                //

                ASSERT(m_buffers);

                thread_local std::vector<TextVertex> vertices;

                text_vertices(m_glyphs, step_y, x, y, text, &vertices);

                const size_t data_size = storage_size(vertices);

                if (m_vertex_buffer->size() < data_size)
                {
                        m_instance.device_wait_idle();

                        m_buffers->delete_command_buffers();

                        m_vertex_buffer.emplace(m_instance.device(), std::max(m_vertex_buffer->size() * 2, data_size));

                        m_buffers->create_command_buffers(std::bind(&Impl::draw_commands, this, std::placeholders::_1));
                }

                m_vertex_buffer->copy(vertices);
                m_indirect_buffer.set(INDIRECT_BUFFER_COMMAND_NUMBER, vertices.size(), 1, 0, 0);

                //

                std::array<VkSemaphore, 1> wait_semaphores = {wait_semaphore};
                std::array<VkPipelineStageFlags, 1> wait_stages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

                VkSubmitInfo info = {};

                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                info.waitSemaphoreCount = wait_semaphores.size();
                info.pWaitSemaphores = wait_semaphores.data();
                info.pWaitDstStageMask = wait_stages.data();
                info.commandBufferCount = 1;
                info.pCommandBuffers = &m_buffers->command_buffer(image_index);
                info.signalSemaphoreCount = 1;
                info.pSignalSemaphores = &finished_semaphore;

                VkResult result = vkQueueSubmit(graphics_queue, 1, &info, queue_fence);
                if (result != VK_SUCCESS)
                {
                        vulkan::vulkan_function_error("vkQueueSubmit", result);
                }
        }

        void draw(VkFence queue_fence, VkQueue graphics_queue, VkSemaphore wait_semaphore, VkSemaphore finished_semaphore,
                  unsigned image_index, int step_y, int x, int y, const std::vector<std::string>& text) override
        {
                draw_text(queue_fence, graphics_queue, wait_semaphore, finished_semaphore, image_index, step_y, x, y, text);
        }

        void draw(VkFence queue_fence, VkQueue graphics_queue, VkSemaphore wait_semaphore, VkSemaphore finished_semaphore,
                  unsigned image_index, int step_y, int x, int y, const std::string& text) override
        {
                draw_text(queue_fence, graphics_queue, wait_semaphore, finished_semaphore, image_index, step_y, x, y, text);
        }

        Impl(const vulkan::VulkanInstance& instance, const Color& color, Glyphs&& glyphs)
                : m_instance(instance),
                  m_sampler(impl::create_text_sampler(instance.device())),
                  m_glyph_texture(instance.create_grayscale_texture(glyphs.width(), glyphs.height(), std::move(glyphs.pixels()))),
                  m_glyphs(std::move(glyphs.glyphs())),
                  m_shader_memory(instance.device(), m_sampler, &m_glyph_texture),
                  m_text_vert(m_instance.device(), vertex_shader, "main"),
                  m_text_frag(m_instance.device(), fragment_shader, "main"),
                  m_pipeline_layout(vulkan::create_pipeline_layout(m_instance.device(), {TEXT_SET_NUMBER},
                                                                   {m_shader_memory.descriptor_set_layout()})),
                  m_vertex_buffer(std::in_place, m_instance.device(), VERTEX_BUFFER_FIRST_SIZE),
                  m_indirect_buffer(m_instance.device(), INDIRECT_BUFFER_COMMAND_COUNT)
        {
                set_color(color);
        }

public:
        Impl(const vulkan::VulkanInstance& instance, int size, const Color& color)
                : Impl(instance, color, Glyphs(size, instance.physical_device().properties().limits.maxImageDimension2D))
        {
        }

        ~Impl() override
        {
                ASSERT(std::this_thread::get_id() == m_thread_id);

                try
                {
                        m_instance.device_wait_idle();
                }
                catch (std::exception& e)
                {
                        LOG(std::string("Device wait idle exception in the Vulkan text destructor: ") + e.what());
                }
                catch (...)
                {
                        LOG("Device wait idle unknown exception in the Vulkan text destructor");
                }
        }
};
}

std::unique_ptr<VulkanText> create_vulkan_text(const vulkan::VulkanInstance& instance, int size, const Color& color)
{
        return std::make_unique<Impl>(instance, size, color);
}
