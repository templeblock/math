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

#if defined(VULKAN_FOUND)

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace vulkan
{
std::string api_version_to_string(uint32_t api_version);
std::string physical_device_type_to_string(VkPhysicalDeviceType type);

void vulkan_function_error[[noreturn]](const std::string& function_name, const VkResult& code);
}

std::vector<const char*> operator+(const std::vector<const char*>& v1, const std::vector<const char*>& v2);
std::vector<const char*> operator+(const std::vector<const char*>& v, const char* s);
std::vector<const char*> operator+(const char* s, const std::vector<const char*>& v);

#endif