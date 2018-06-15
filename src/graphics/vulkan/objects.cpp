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

#include "objects.h"

#include "application/application_name.h"
#include "com/error.h"
#include "com/print.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
std::string api_version_to_string(uint32_t api_version)
{
        std::ostringstream oss;
        oss << VK_VERSION_MAJOR(api_version) << "." << VK_VERSION_MINOR(api_version) << "." << VK_VERSION_PATCH(api_version);
        return oss.str();
}

std::array<std::string, 2> return_code_strings(const VkResult& code)
{
        switch (code)
        {
        case VK_SUCCESS:
                return {"VK_SUCCESS", "Command successfully completed"};
        case VK_NOT_READY:
                return {"VK_NOT_READY", "A fence or query has not yet completed"};
        case VK_TIMEOUT:
                return {"VK_TIMEOUT", "A wait operation has not completed in the specified time"};
        case VK_EVENT_SET:
                return {"VK_EVENT_SET", "An event is signaled"};
        case VK_EVENT_RESET:
                return {"VK_EVENT_RESET", "An event is unsignaled"};
        case VK_INCOMPLETE:
                return {"VK_INCOMPLETE", "A return array was too small for the result"};
        case VK_ERROR_OUT_OF_HOST_MEMORY:
                return {"VK_ERROR_OUT_OF_HOST_MEMORY", "A host memory allocation has failed"};
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                return {"VK_ERROR_OUT_OF_DEVICE_MEMORY", "A device memory allocation has failed"};
        case VK_ERROR_INITIALIZATION_FAILED:
                return {"VK_ERROR_INITIALIZATION_FAILED",
                        "Initialization of an object could not be completed for implementation-specific reasons"};
        case VK_ERROR_DEVICE_LOST:
                return {"VK_ERROR_DEVICE_LOST", "The logical or physical device has been lost"};
        case VK_ERROR_MEMORY_MAP_FAILED:
                return {"VK_ERROR_MEMORY_MAP_FAILED", "Mapping of a memory object has failed"};
        case VK_ERROR_LAYER_NOT_PRESENT:
                return {"VK_ERROR_LAYER_NOT_PRESENT", "A requested layer is not present or could not be loaded"};
        case VK_ERROR_EXTENSION_NOT_PRESENT:
                return {"VK_ERROR_EXTENSION_NOT_PRESENT", "A requested extension is not supported"};
        case VK_ERROR_FEATURE_NOT_PRESENT:
                return {"VK_ERROR_FEATURE_NOT_PRESENT", "A requested feature is not supported"};
        case VK_ERROR_INCOMPATIBLE_DRIVER:
                return {"VK_ERROR_INCOMPATIBLE_DRIVER",
                        "The requested version of Vulkan is not supported by the driver or is otherwise incompatible"
                        " for implementation-specific reasons"};
        case VK_ERROR_TOO_MANY_OBJECTS:
                return {"VK_ERROR_TOO_MANY_OBJECTS", "Too many objects of the type have already been created"};
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
                return {"VK_ERROR_FORMAT_NOT_SUPPORTED", "A requested format is not supported on this device"};
        case VK_ERROR_FRAGMENTED_POOL:
                return {"VK_ERROR_FRAGMENTED_POOL", "A pool allocation has failed due to fragmentation of the pool’s memory"};
        case VK_ERROR_OUT_OF_POOL_MEMORY:
                return {"VK_ERROR_OUT_OF_POOL_MEMORY", "A pool memory allocation has failed"};
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
                return {"VK_ERROR_INVALID_EXTERNAL_HANDLE", "An external handle is not a valid handle of the specified type"};
        case VK_ERROR_SURFACE_LOST_KHR:
                return {"VK_ERROR_SURFACE_LOST_KHR", "A surface is no longer available"};
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
                return {"VK_ERROR_NATIVE_WINDOW_IN_USE_KHR",
                        "The requested window is already in use by Vulkan or another API in a manner which prevents"
                        " it from being used again"};
        case VK_SUBOPTIMAL_KHR:
                return {"VK_SUBOPTIMAL_KHR",
                        "A swapchain no longer matches the surface properties exactly, but can still be used to present"
                        " to the surface successfully"};
        case VK_ERROR_OUT_OF_DATE_KHR:
                return {"VK_ERROR_OUT_OF_DATE_KHR",
                        "A surface has changed in such a way that it is no longer compatible with the swapchain, and further"
                        " presentation requests using the swapchain will fail"};
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
                return {"VK_ERROR_INCOMPATIBLE_DISPLAY_KHR",
                        "The display used by a swapchain does not use the same presentable image layout, or is incompatible"
                        " in a way that prevents sharing an image"};
        case VK_ERROR_VALIDATION_FAILED_EXT:
                return {"VK_ERROR_VALIDATION_FAILED_EXT", ""};
        case VK_ERROR_INVALID_SHADER_NV:
                return {"VK_ERROR_INVALID_SHADER_NV", ""};
        case VK_ERROR_FRAGMENTATION_EXT:
                return {"VK_ERROR_FRAGMENTATION_EXT", ""};
        case VK_ERROR_NOT_PERMITTED_EXT:
                return {"VK_ERROR_NOT_PERMITTED_EXT", ""};
        case VK_RESULT_RANGE_SIZE:
                return {"VK_RESULT_RANGE_SIZE", ""};
        case VK_RESULT_MAX_ENUM:
                return {"VK_RESULT_MAX_ENUM", ""};
        }
        return {"Unknown Vulkan return code " + to_string(static_cast<long long>(code)), ""};
}

std::string return_code_string(const std::string& function_name, const VkResult& code)
{
        std::array<std::string, 2> strings = return_code_strings(code);

        std::string result;

        if (function_name.size() > 0)
        {
                result += function_name + ".";
        }

        for (const std::string& s : strings)
        {
                if (s.size() > 0)
                {
                        result += " " + s + ".";
                }
        }

        if (result.size() > 0)
        {
                return result;
        }

        static_assert(sizeof(VkResult) <= sizeof(long long));
        return "Vulkan Return Code " + to_string(static_cast<long long>(code));
}

// clang-format 6 неправильно форматирует, если [[noreturn]] поставить перед функцией
void vulkan_function_error[[noreturn]](const std::string& function_name, const VkResult& code)
{
        error("Vulkan Error. " + return_code_string(function_name, code));
}

std::unordered_set<std::string> supported_extensions()
{
        uint32_t extension_count;
        VkResult result;

        result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS)
        {
                vulkan_function_error("vkEnumerateInstanceExtensionProperties", result);
        }

        std::vector<VkExtensionProperties> extensions(extension_count);

        result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
        if (result != VK_SUCCESS)
        {
                vulkan_function_error("vkEnumerateInstanceExtensionProperties", result);
        }

        std::unordered_set<std::string> extension_set;

        for (const VkExtensionProperties& e : extensions)
        {
                extension_set.emplace(e.extensionName);
        }

        return extension_set;
}

uint32_t supported_api_version()
{
        if (!vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"))
        {
                return VK_MAKE_VERSION(1, 0, 0);
        }

        uint32_t api_version;
        VkResult result = vkEnumerateInstanceVersion(&api_version);
        if (result != VK_SUCCESS)
        {
                vulkan_function_error("vkEnumerateInstanceVersion", result);
        }

        return api_version;
}

void check_extension_support(const std::vector<const char*>& required_extensions)
{
        const std::unordered_set<std::string> extension_set = supported_extensions();

        for (const std::string& e : required_extensions)
        {
                if (extension_set.count(e) < 1)
                {
                        error("Vulkan extension " + e + " is not supported");
                }
        }
}

void check_api_version(uint32_t required_api_version)
{
        uint32_t api_version = supported_api_version();

        if (required_api_version > api_version)
        {
                error("Vulkan API version " + api_version_to_string(required_api_version) + " is not supported. Supported " +
                      api_version_to_string(api_version) + ".");
        }
}
}

namespace vulkan
{
std::string overview()
{
        std::string s;

        s += "API version ";
        try
        {
                s += api_version_to_string(supported_api_version());
        }
        catch (std::exception& e)
        {
                s += e.what();
        }

        try
        {
                std::unordered_set<std::string> extensions(supported_extensions());
                std::vector<std::string> extension_vector(extensions.begin(), extensions.end());
                std::sort(extension_vector.begin(), extension_vector.end());
                for (const std::string& extension : extension_vector)
                {
                        s += "\n";
                        s += extension;
                }
        }
        catch (std::exception& e)
        {
                s += "\n";
                s += e.what();
        }

        return s;
}

Instance::Instance(int api_version_major, int api_version_minor, const std::vector<const char*>& required_extensions)
{
        const uint32_t api_version = VK_MAKE_VERSION(api_version_major, api_version_minor, 0);

        check_api_version(api_version);
        check_extension_support(required_extensions);

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = APPLICATION_NAME;
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = nullptr;
        appInfo.engineVersion = 0;
        appInfo.apiVersion = api_version;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = required_extensions.size();
        createInfo.ppEnabledExtensionNames = required_extensions.data();
        createInfo.enabledLayerCount = 0;

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
        if (result != VK_SUCCESS)
        {
                vulkan_function_error("vkCreateInstance", result);
        }
}

Instance::~Instance()
{
        vkDestroyInstance(m_instance, nullptr);
}
}

#endif
