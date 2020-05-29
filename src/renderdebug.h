#define VULKAN_HPP_TYPESAFE_CONVERSION
#include <vulkan/vulkan.hpp>

#include <iostream>

static PFN_vkCreateDebugUtilsMessengerEXT  proxy_create_debugger;
static PFN_vkDestroyDebugUtilsMessengerEXT proxy_destroy_debugger;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT *create_info,
            const VkAllocationCallbacks *allocator,
            VkDebugUtilsMessengerEXT *messenger);

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
            VkInstance instance,
            VkDebugUtilsMessengerEXT messenger,
            VkAllocationCallbacks const *allocator);

class RenderDebug {
    vk::UniqueDebugUtilsMessengerEXT messenger_;

    static VKAPI_ATTR VkBool32 VKAPI_CALL message_callback(
                VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                VkDebugUtilsMessageTypeFlagsEXT type,
                VkDebugUtilsMessengerCallbackDataEXT const *data,
                void *user_data);

    void load_proxies(vk::UniqueInstance &instance);

public:
    RenderDebug(vk::UniqueInstance &instance);
};