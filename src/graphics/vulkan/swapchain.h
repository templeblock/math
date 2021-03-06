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

#include "objects.h"

#include <vector>

namespace vulkan
{
enum class PresentMode
{
        PreferSync,
        PreferFast
};

class Swapchain
{
        VkSurfaceFormatKHR m_surface_format;
        VkExtent2D m_extent;
        SwapchainKHR m_swapchain;
        std::vector<VkImage> m_images;
        std::vector<ImageView> m_image_views;

public:
        Swapchain(VkSurfaceKHR surface, const Device& device, const std::vector<uint32_t>& family_indices,
                  const VkSurfaceFormatKHR& required_surface_format, int preferred_image_count,
                  PresentMode preferred_present_mode);

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        Swapchain& operator=(Swapchain&&) = delete;

        Swapchain(Swapchain&&) = default;
        ~Swapchain() = default;

        //

        VkSwapchainKHR swapchain() const noexcept;

        uint32_t width() const noexcept;
        uint32_t height() const noexcept;
        VkFormat format() const noexcept;
        VkColorSpaceKHR color_space() const noexcept;
        const std::vector<ImageView>& image_views() const noexcept;
};

bool surface_suitable(VkSurfaceKHR surface, VkPhysicalDevice device);
}
