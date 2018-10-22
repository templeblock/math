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

#pragma once

#include "buffers.h"
#include "device.h"
#include "objects.h"
#include "shader.h"
#include "swapchain.h"

#include "com/color/color.h"
#include "com/type_detect.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vulkan
{
class Buffers
{
        const Device& m_device;
        VkCommandPool m_graphics_command_pool;
        VkFormat m_swapchain_format;
        VkColorSpaceKHR m_swapchain_color_space;

        //

        std::unique_ptr<DepthAttachment> m_depth_attachment;
        std::unique_ptr<ColorAttachment> m_color_attachment;
        RenderPass m_render_pass;
        std::vector<Framebuffer> m_framebuffers;
        std::vector<Pipeline> m_pipelines;
        CommandBuffers m_command_buffers;

        std::unique_ptr<ShadowDepthAttachment> m_shadow_depth_attachment;
        RenderPass m_shadow_render_pass;
        std::vector<Framebuffer> m_shadow_framebuffers;
        std::vector<Pipeline> m_shadow_pipelines;
        CommandBuffers m_shadow_command_buffers;

        std::string main_buffer_info_string() const;
        std::string shadow_buffer_info_string(double zoom, unsigned preferred_width, unsigned preferred_height) const;

        void create_main_buffers(const Swapchain& swapchain, const std::vector<uint32_t>& attachment_family_indices,
                                 VkQueue graphics_queue, const std::vector<VkFormat>& depth_image_formats,
                                 VkSampleCountFlagBits sample_count);

        void create_shadow_buffers(unsigned width, unsigned height, const std::vector<uint32_t>& attachment_family_indices,
                                   VkQueue graphics_queue, const std::vector<VkFormat>& depth_image_formats, double shadow_zoom);

public:
        Buffers(const Swapchain& swapchain, const std::vector<uint32_t>& attachment_family_indices, const Device& device,
                VkCommandPool graphics_command_pool, VkQueue graphics_queue, int required_minimum_sample_count,
                const std::vector<VkFormat>& depth_image_formats, double shadow_zoom);

        Buffers(const Buffers&) = delete;
        Buffers& operator=(const Buffers&) = delete;
        Buffers& operator=(Buffers&&) = delete;

        Buffers(Buffers&&) = default;
        ~Buffers() = default;

        //

        const ShadowDepthAttachment* shadow_texture() const noexcept;

        void create_command_buffers(const Color& clear_color, const std::function<void(VkCommandBuffer buffer)>& commands);
        void create_shadow_command_buffers(const std::function<void(VkCommandBuffer buffer)>& shadow_commands);

        void delete_command_buffers();
        void delete_shadow_command_buffers();

        const VkCommandBuffer& command_buffer(uint32_t index) const noexcept;
        const VkCommandBuffer& shadow_command_buffer() const noexcept;

        VkPipeline create_pipeline(VkPrimitiveTopology primitive_topology, const std::vector<const vulkan::Shader*>& shaders,
                                   const PipelineLayout& pipeline_layout,
                                   const std::vector<VkVertexInputBindingDescription>& vertex_binding_descriptions,
                                   const std::vector<VkVertexInputAttributeDescription>& vertex_attribute_descriptions);
        VkPipeline create_shadow_pipeline(VkPrimitiveTopology primitive_topology,
                                          const std::vector<const vulkan::Shader*>& shaders,
                                          const PipelineLayout& pipeline_layout,
                                          const std::vector<VkVertexInputBindingDescription>& vertex_binding_descriptions,
                                          const std::vector<VkVertexInputAttributeDescription>& vertex_attribute_descriptions);

        VkSwapchainKHR swapchain() const noexcept;
        VkFormat swapchain_format() const noexcept;
        VkColorSpaceKHR swapchain_color_space() const noexcept;
};

class VulkanInstance
{
        Instance m_instance;
        std::optional<DebugReportCallback> m_callback;

        SurfaceKHR m_surface;

        PhysicalDevice m_physical_device;

        Device m_device;

        CommandPool m_graphics_command_pool;
        VkQueue m_graphics_queue = VK_NULL_HANDLE;

        CommandPool m_transfer_command_pool;
        VkQueue m_transfer_queue = VK_NULL_HANDLE;

        VkQueue m_compute_queue = VK_NULL_HANDLE;
        VkQueue m_presentation_queue = VK_NULL_HANDLE;

        std::vector<uint32_t> m_buffer_family_indices;
        std::vector<uint32_t> m_swapchain_family_indices;
        std::vector<uint32_t> m_texture_family_indices;
        std::vector<uint32_t> m_attachment_family_indices;

public:
        VulkanInstance(const std::vector<std::string>& required_instance_extensions,
                       const std::vector<std::string>& required_device_extensions,
                       const std::vector<PhysicalDeviceFeatures>& required_features,
                       const std::vector<PhysicalDeviceFeatures>& optional_features,
                       const std::function<VkSurfaceKHR(VkInstance)>& create_surface);

        ~VulkanInstance();

        VulkanInstance(const VulkanInstance&) = delete;
        VulkanInstance(VulkanInstance&&) = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;
        VulkanInstance& operator=(VulkanInstance&&) = delete;

        //

        void device_wait_idle() const;

        //

        VkInstance instance() const noexcept
        {
                return m_instance;
        }

        const Device& device() const noexcept
        {
                return m_device;
        }

        VkQueue presentation_queue() const noexcept
        {
                return m_presentation_queue;
        }

        VkQueue graphics_queue() const noexcept
        {
                return m_graphics_queue;
        }

        VkCommandPool graphics_command_pool() const noexcept
        {
                return m_graphics_command_pool;
        }

        const std::vector<uint32_t>& attachment_family_indices() const noexcept
        {
                return m_attachment_family_indices;
        }

        //

        Swapchain create_swapchain(VkSurfaceFormatKHR required_surface_format, int preferred_image_count)
        {
                return Swapchain(m_surface, m_device, m_swapchain_family_indices, required_surface_format, preferred_image_count);
        }

        template <typename T>
        VertexBufferWithDeviceLocalMemory create_vertex_buffer(const T& data) const
        {
                static_assert(is_vector<T> || is_array<T>);
                return VertexBufferWithDeviceLocalMemory(m_device, m_transfer_command_pool, m_transfer_queue,
                                                         m_buffer_family_indices, data.size() * sizeof(typename T::value_type),
                                                         data.data());
        }

        template <typename T>
        IndexBufferWithDeviceLocalMemory create_index_buffer(const T& data) const
        {
                static_assert(is_vector<T> || is_array<T>);
                return IndexBufferWithDeviceLocalMemory(m_device, m_transfer_command_pool, m_transfer_queue,
                                                        m_buffer_family_indices, data.size() * sizeof(typename T::value_type),
                                                        data.data());
        }

        template <typename T>
        ColorTexture create_texture(uint32_t width, uint32_t height, const std::vector<T>& rgba_pixels) const
        {
                return ColorTexture(m_device, m_graphics_command_pool, m_graphics_queue, m_transfer_command_pool,
                                    m_transfer_queue, m_texture_family_indices, width, height, rgba_pixels);
        }
};
}
