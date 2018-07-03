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

#if defined(VULKAN_FOUND)

#include "instance.h"

#include "common.h"
#include "debug.h"
#include "query.h"

#include "application/application_name.h"
#include "com/error.h"

namespace
{
VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& surface_formats)
{
        ASSERT(surface_formats.size() > 0);

        if (surface_formats.size() == 1 && surface_formats[0].format == VK_FORMAT_UNDEFINED)
        {
                return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }

        for (const VkSurfaceFormatKHR& format : surface_formats)
        {
                if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                        return format;
                }
        }

        return surface_formats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& present_modes)
{
        for (const VkPresentModeKHR& present_mode : present_modes)
        {
                if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                        return present_mode;
                }
        }

        for (const VkPresentModeKHR& present_mode : present_modes)
        {
                if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                        return present_mode;
                }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities)
{
        if (!(capabilities.currentExtent.width == 0xffff'ffff && capabilities.currentExtent.height == 0xffff'ffff))
        {
                return capabilities.currentExtent;
        }

        error("Current width and height of the surface arer not defined");

#if 0
        VkExtent2D extent;
        extent.width = std::clamp(1u, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(1u, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
#endif
}

uint32_t choose_image_count(const VkSurfaceCapabilitiesKHR& capabilities)
{
        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0)
        {
                image_count = std::min(image_count, capabilities.maxImageCount);
        }
        return image_count;
}

struct SwapChainDetails
{
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> surface_formats;
        std::vector<VkPresentModeKHR> present_modes;
};

bool find_swap_chain_details(VkPhysicalDevice device, VkSurfaceKHR surface, SwapChainDetails* swap_chain_details)
{
        VkSurfaceCapabilitiesKHR capabilities;

        VkResult result;
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
        if (result != VK_SUCCESS)
        {
                vulkan::vulkan_function_error("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        }

        std::vector<VkSurfaceFormatKHR> surface_formats = vulkan::surface_formats(device, surface);
        if (surface_formats.empty())
        {
                return false;
        }

        std::vector<VkPresentModeKHR> present_modes = vulkan::present_modes(device, surface);
        if (present_modes.empty())
        {
                return false;
        }

        swap_chain_details->capabilities = capabilities;
        swap_chain_details->surface_formats = surface_formats;
        swap_chain_details->present_modes = present_modes;

        return true;
}

struct FoundPhysicalDevice
{
        const VkPhysicalDevice physical_device;
        const unsigned graphics_family_index;
        const unsigned compute_family_index;
        const unsigned presentation_family_index;
        SwapChainDetails swap_chain_details;
};

FoundPhysicalDevice find_physical_device(VkInstance instance, VkSurfaceKHR surface, int api_version_major, int api_version_minor,
                                         const std::vector<std::string>& required_extensions)
{
        const uint32_t required_api_version = VK_MAKE_VERSION(api_version_major, api_version_minor, 0);

        for (const VkPhysicalDevice& device : vulkan::physical_devices(instance))
        {
                VkPhysicalDeviceProperties properties;
                VkPhysicalDeviceFeatures features;
                vkGetPhysicalDeviceProperties(device, &properties);
                vkGetPhysicalDeviceFeatures(device, &features);

                if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                    properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
                    properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU &&
                    properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
                {
                        continue;
                }

                if (!features.geometryShader)
                {
                        continue;
                }

                if (!features.tessellationShader)
                {
                        continue;
                }

                if (required_api_version > properties.apiVersion)
                {
                        continue;
                }

                if (!vulkan::device_supports_extensions(device, required_extensions))
                {
                        continue;
                }

                unsigned index = 0;
                unsigned graphics_family_index = 0;
                unsigned compute_family_index = 0;
                unsigned presentation_family_index = 0;
                bool graphics_found = false;
                bool compute_found = false;
                bool presentation_found = false;
                for (const VkQueueFamilyProperties& p : vulkan::queue_families(device))
                {
                        if (p.queueCount < 1)
                        {
                                continue;
                        }

                        if (!graphics_found && (p.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                        {
                                graphics_found = true;
                                graphics_family_index = index;
                        }

                        if (!compute_found && (p.queueFlags & VK_QUEUE_COMPUTE_BIT))
                        {
                                compute_found = true;
                                compute_family_index = index;
                        }

                        if (!presentation_found)
                        {
                                VkBool32 presentation_support;

                                VkResult result =
                                        vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentation_support);
                                if (result != VK_SUCCESS)
                                {
                                        vulkan::vulkan_function_error("vkGetPhysicalDeviceSurfaceSupportKHR", result);
                                }

                                if (presentation_support == VK_TRUE)
                                {
                                        presentation_found = true;
                                        presentation_family_index = index;
                                }
                        }

                        if (graphics_found && compute_found && presentation_found)
                        {
                                break;
                        }

                        ++index;
                }

                if (!graphics_found || !compute_found || !presentation_found)
                {
                        continue;
                }

                SwapChainDetails swap_chain_details;
                if (!find_swap_chain_details(device, surface, &swap_chain_details))
                {
                        continue;
                }

                return {device, graphics_family_index, compute_family_index, presentation_family_index, swap_chain_details};
        }

        error("Failed to find a suitable Vulkan physical device");
}

std::vector<VkPipelineShaderStageCreateInfo> pipeline_shader_stage_create_info(const std::vector<const vulkan::Shader*>& shaders)
{
        std::vector<VkPipelineShaderStageCreateInfo> res;

        for (const vulkan::Shader* s : shaders)
        {
                VkPipelineShaderStageCreateInfo stage_info = {};
                stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stage_info.stage = s->stage();
                stage_info.module = s->module();
                stage_info.pName = "main";

                res.push_back(stage_info);
        }

        return res;
}

vulkan::Instance create_instance(int api_version_major, int api_version_minor, std::vector<std::string> required_extensions,
                                 const std::vector<std::string>& required_validation_layers)
{
        const uint32_t required_api_version = VK_MAKE_VERSION(api_version_major, api_version_minor, 0);

        if (required_validation_layers.size() > 0)
        {
                required_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }

        vulkan::check_api_version(required_api_version);
        vulkan::check_instance_extension_support(required_extensions);
        vulkan::check_validation_layer_support(required_validation_layers);

        VkApplicationInfo app_info = {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = APPLICATION_NAME;
        app_info.applicationVersion = 1;
        app_info.pEngineName = nullptr;
        app_info.engineVersion = 0;
        app_info.apiVersion = required_api_version;

        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;

        const std::vector<const char*> extensions = to_char_pointer_vector(required_extensions);
        if (extensions.size() > 0)
        {
                create_info.enabledExtensionCount = extensions.size();
                create_info.ppEnabledExtensionNames = extensions.data();
        }

        const std::vector<const char*> validation_layers = to_char_pointer_vector(required_validation_layers);
        if (validation_layers.size() > 0)
        {
                create_info.enabledLayerCount = validation_layers.size();
                create_info.ppEnabledLayerNames = validation_layers.data();
        }

        return vulkan::Instance(create_info);
}

vulkan::Device create_device(VkPhysicalDevice physical_device, const std::vector<unsigned>& family_indices,
                             const std::vector<std::string>& required_extensions,
                             const std::vector<std::string>& required_validation_layers)
{
        if (family_indices.empty())
        {
                error("No family indices for device creation");
        }

        constexpr float queue_priority = 1;
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        for (unsigned unique_queue_family_index : std::unordered_set<unsigned>(family_indices.cbegin(), family_indices.cend()))
        {
                VkDeviceQueueCreateInfo queue_create_info = {};
                queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queue_create_info.queueFamilyIndex = unique_queue_family_index;
                queue_create_info.queueCount = 1;
                queue_create_info.pQueuePriorities = &queue_priority;

                queue_create_infos.push_back(queue_create_info);
        }

        VkPhysicalDeviceFeatures device_features = {};

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = queue_create_infos.size();
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.pEnabledFeatures = &device_features;

        const std::vector<const char*> extensions = to_char_pointer_vector(required_extensions);
        if (extensions.size() > 0)
        {
                create_info.enabledExtensionCount = extensions.size();
                create_info.ppEnabledExtensionNames = extensions.data();
        }

        const std::vector<const char*> validation_layers = to_char_pointer_vector(required_validation_layers);
        if (validation_layers.size() > 0)
        {
                create_info.enabledLayerCount = validation_layers.size();
                create_info.ppEnabledLayerNames = validation_layers.data();
        }

        return vulkan::Device(physical_device, create_info);
}

vulkan::SwapChainKHR create_swap_chain_khr(VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format,
                                           VkPresentModeKHR present_mode, VkExtent2D extent, uint32_t image_count,
                                           VkSurfaceTransformFlagBitsKHR transform, unsigned graphics_family_index,
                                           unsigned presentation_family_index)
{
        VkSwapchainCreateInfoKHR create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface;

        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        std::vector<uint32_t> family_indices;
        if (graphics_family_index != presentation_family_index)
        {
                family_indices = {graphics_family_index, presentation_family_index};
                create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                create_info.queueFamilyIndexCount = family_indices.size();
                create_info.pQueueFamilyIndices = family_indices.data();
        }
        else
        {
                create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        create_info.preTransform = transform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;

        create_info.oldSwapchain = VK_NULL_HANDLE;

        return vulkan::SwapChainKHR(device, create_info);
}

vulkan::ImageView create_image_view(VkDevice device, VkImage image, VkFormat format)
{
        VkImageViewCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = image;

        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = format;

        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        return vulkan::ImageView(device, create_info);
}

vulkan::RenderPass create_render_pass(VkDevice device, VkFormat swap_chain_image_format)
{
        VkAttachmentDescription attachment_description = {};
        attachment_description.format = swap_chain_image_format;
        attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;

        attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        attachment_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment_description.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference attachment_reference = {};
        attachment_reference.attachment = 0;
        attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass_description = {};
        subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_description.colorAttachmentCount = 1;
        subpass_description.pColorAttachments = &attachment_reference;

        VkRenderPassCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &attachment_description;
        create_info.subpassCount = 1;
        create_info.pSubpasses = &subpass_description;

        return vulkan::RenderPass(device, create_info);
}

vulkan::PipelineLayout create_pipeline_layout(VkDevice device)
{
        VkPipelineLayoutCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        // create_info.setLayoutCount = 0;
        // create_info.pSetLayouts = nullptr;
        // create_info.pushConstantRangeCount = 0;
        // create_info.pPushConstantRanges = nullptr;

        return vulkan::PipelineLayout(device, create_info);
}

vulkan::Pipeline create_graphics_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout pipeline_layout,
                                          VkExtent2D swap_chain_extent, const Span<const uint32_t>& vertex_shader_code,
                                          const Span<const uint32_t>& fragment_shader_code)
{
        vulkan::VertexShader vertex_shader(device, vertex_shader_code);
        vulkan::FragmentShader fragment_shader(device, fragment_shader_code);

        std::vector<VkPipelineShaderStageCreateInfo> pipeline_shader_stages =
                pipeline_shader_stage_create_info({&vertex_shader, &fragment_shader});

        VkPipelineVertexInputStateCreateInfo vertex_input_state_info = {};
        vertex_input_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state_info.vertexBindingDescriptionCount = 0;
        vertex_input_state_info.pVertexBindingDescriptions = nullptr;
        vertex_input_state_info.vertexAttributeDescriptionCount = 0;
        vertex_input_state_info.pVertexAttributeDescriptions = nullptr;

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info = {};
        input_assembly_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly_state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly_state_info.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = swap_chain_extent.width;
        viewport.height = swap_chain_extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = swap_chain_extent;

        VkPipelineViewportStateCreateInfo viewport_state_info = {};
        viewport_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_info.viewportCount = 1;
        viewport_state_info.pViewports = &viewport;
        viewport_state_info.scissorCount = 1;
        viewport_state_info.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterization_state_info = {};
        rasterization_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_state_info.depthClampEnable = VK_FALSE;

        rasterization_state_info.rasterizerDiscardEnable = VK_FALSE;

        rasterization_state_info.polygonMode = VK_POLYGON_MODE_FILL;

        rasterization_state_info.lineWidth = 1.0f;

        rasterization_state_info.cullMode = VK_CULL_MODE_NONE;
        rasterization_state_info.frontFace = VK_FRONT_FACE_CLOCKWISE;

        rasterization_state_info.depthBiasEnable = VK_FALSE;
        // rasterization_state_info.depthBiasConstantFactor = 0.0f;
        // rasterization_state_info.depthBiasClamp = 0.0f;
        // rasterization_state_info.depthBiasSlopeFactor = 0.0f;

        VkPipelineMultisampleStateCreateInfo multisampling_state_info = {};
        multisampling_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling_state_info.sampleShadingEnable = VK_FALSE;
        multisampling_state_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // multisampling_state_info.minSampleShading = 1.0f;
        // multisampling_state_info.pSampleMask = nullptr;
        // multisampling_state_info.alphaToCoverageEnable = VK_FALSE;
        // multisampling_state_info.alphaToOneEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
        color_blend_attachment_state.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (true)
        {
                color_blend_attachment_state.blendEnable = VK_FALSE;
                // color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                // color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                // color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
                // color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                // color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                // color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        else
        {
                color_blend_attachment_state.blendEnable = VK_TRUE;
                color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
                color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo color_blending_state_info = {};
        color_blending_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending_state_info.logicOpEnable = VK_FALSE;
        // color_blending_state_info.logicOp = VK_LOGIC_OP_COPY;
        color_blending_state_info.attachmentCount = 1;
        color_blending_state_info.pAttachments = &color_blend_attachment_state;
        // color_blending_state_info.blendConstants[0] = 0.0f;
        // color_blending_state_info.blendConstants[1] = 0.0f;
        // color_blending_state_info.blendConstants[2] = 0.0f;
        // color_blending_state_info.blendConstants[3] = 0.0f;

        // std::vector<VkDynamicState> dynamic_states({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH});
        // VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
        // dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        // dynamic_state_info.dynamicStateCount = dynamic_states.size();
        // dynamic_state_info.pDynamicStates = dynamic_states.data();

        VkGraphicsPipelineCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create_info.stageCount = pipeline_shader_stages.size();
        create_info.pStages = pipeline_shader_stages.data();

        create_info.pVertexInputState = &vertex_input_state_info;
        create_info.pInputAssemblyState = &input_assembly_state_info;
        create_info.pViewportState = &viewport_state_info;
        create_info.pRasterizationState = &rasterization_state_info;
        create_info.pMultisampleState = &multisampling_state_info;
        // create_info.pDepthStencilState = nullptr;
        create_info.pColorBlendState = &color_blending_state_info;
        // create_info.pDynamicState = nullptr;

        create_info.layout = pipeline_layout;

        create_info.renderPass = render_pass;
        create_info.subpass = 0;

        // create_info.basePipelineHandle = VK_NULL_HANDLE;
        // create_info.basePipelineIndex = -1;

        return vulkan::Pipeline(device, create_info);
}

vulkan::Framebuffer create_framebuffer(VkDevice device, VkRenderPass render_pass, VkImageView attachment, VkExtent2D extent)
{
        VkFramebufferCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass = render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &attachment;
        create_info.width = extent.width;
        create_info.height = extent.height;
        create_info.layers = 1;

        return vulkan::Framebuffer(device, create_info);
}
}

namespace vulkan
{
VulkanInstance::VulkanInstance(int api_version_major, int api_version_minor,
                               const std::vector<std::string>& required_instance_extensions,
                               const std::vector<std::string>& required_device_extensions,
                               const std::vector<std::string>& required_validation_layers,
                               const std::function<VkSurfaceKHR(VkInstance)>& create_surface,
                               const Span<const uint32_t>& vertex_shader_code, const Span<const uint32_t>& fragment_shader_code)
        : m_instance(create_instance(api_version_major, api_version_minor, required_instance_extensions,
                                     required_validation_layers)),
          m_callback(!required_validation_layers.empty() ? std::make_optional(create_debug_report_callback(m_instance)) :
                                                           std::nullopt),
          m_surface(m_instance, create_surface)
{
        const std::vector<std::string> all_device_extensions = required_device_extensions + VK_KHR_SWAPCHAIN_EXTENSION_NAME;

        FoundPhysicalDevice device =
                find_physical_device(m_instance, m_surface, api_version_major, api_version_minor, all_device_extensions);

        m_physical_device = device.physical_device;

        ASSERT(m_physical_device != VK_NULL_HANDLE);

        m_device = create_device(device.physical_device,
                                 {device.graphics_family_index, device.compute_family_index, device.presentation_family_index},
                                 all_device_extensions, required_validation_layers);

        constexpr uint32_t queue_index = 0;
        vkGetDeviceQueue(m_device, device.graphics_family_index, queue_index, &m_graphics_queue);
        vkGetDeviceQueue(m_device, device.compute_family_index, queue_index, &m_compute_queue);
        vkGetDeviceQueue(m_device, device.presentation_family_index, queue_index, &m_presentation_queue);

        ASSERT(m_graphics_queue != VK_NULL_HANDLE);
        ASSERT(m_compute_queue != VK_NULL_HANDLE);
        ASSERT(m_presentation_queue != VK_NULL_HANDLE);

        VkSurfaceFormatKHR surface_format = choose_surface_format(device.swap_chain_details.surface_formats);
        m_swap_chain_extent = choose_extent(device.swap_chain_details.capabilities);
        m_swap_chain_image_format = surface_format.format;

        m_swap_chain = create_swap_chain_khr(m_device, m_surface, surface_format,
                                             choose_present_mode(device.swap_chain_details.present_modes), m_swap_chain_extent,
                                             choose_image_count(device.swap_chain_details.capabilities),
                                             device.swap_chain_details.capabilities.currentTransform,
                                             device.graphics_family_index, device.presentation_family_index);

        m_swap_chain_images = swap_chain_images(m_device, m_swap_chain);
        if (m_swap_chain_images.empty())
        {
                error("Failed to find swap chain images");
        }

        for (const VkImage& image : m_swap_chain_images)
        {
                m_image_views.push_back(create_image_view(m_device, image, m_swap_chain_image_format));
        }

        m_render_pass = create_render_pass(m_device, m_swap_chain_image_format);
        m_pipeline_layout = create_pipeline_layout(m_device);
        m_pipeline = create_graphics_pipeline(m_device, m_render_pass, m_pipeline_layout, m_swap_chain_extent, vertex_shader_code,
                                              fragment_shader_code);

        for (const ImageView& image_view : m_image_views)
        {
                m_framebuffers.push_back(create_framebuffer(m_device, m_render_pass, image_view, m_swap_chain_extent));
        }
}

VkInstance VulkanInstance::instance() const
{
        return m_instance;
}
}

#endif
