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

#include <string>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

namespace vulkan
{
std::unordered_set<std::string> supported_instance_extensions();
std::unordered_set<std::string> supported_validation_layers();

uint32_t supported_instance_api_version();

void check_instance_extension_support(const std::vector<std::string>& required_extensions);
void check_validation_layer_support(const std::vector<std::string>& required_layers);
void check_api_version(uint32_t required_api_version);

VkFormat find_supported_format(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                               VkFormatFeatureFlags features);
VkFormat find_supported_2d_image_format(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates,
                                        VkImageTiling tiling, VkFormatFeatureFlags features, VkImageUsageFlags usage,
                                        VkSampleCountFlags sample_count);
VkExtent2D max_2d_image_extent(VkPhysicalDevice physical_device, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage);

VkSampleCountFlagBits supported_framebuffer_sample_count_flag(VkPhysicalDevice physical_device,
                                                              int required_minimum_sample_count);
int integer_sample_count_flag(VkSampleCountFlagBits sample_count);
}
