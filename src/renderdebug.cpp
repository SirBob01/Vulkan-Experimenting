#include "renderdebug.h"

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT *create_info,
            const VkAllocationCallbacks *allocator,
            VkDebugUtilsMessengerEXT *messenger) {
    return proxy_create_debugger(
        instance, create_info, allocator, messenger
    );
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
            VkInstance instance,
            VkDebugUtilsMessengerEXT messenger,
            VkAllocationCallbacks const *allocator) {
    proxy_destroy_debugger(instance, messenger, allocator);
}

VKAPI_ATTR VkBool32 VKAPI_CALL RenderDebug::message_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            VkDebugUtilsMessengerCallbackDataEXT const *data,
            void *user_data) {
    std::cerr << "---- Debugger Warning ----" << "\n";
    std::cerr << "Message name: " << data->pMessageIdName << "\n";
    std::cerr << "Message ID: " << data->messageIdNumber << "\n";
    std::cerr << data->pMessage << "\n\n";
    return false;
}

void RenderDebug::load_proxies(vk::UniqueInstance &instance) {
    proxy_create_debugger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        instance->getProcAddr("vkCreateDebugUtilsMessengerEXT")
    );
    proxy_destroy_debugger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        instance->getProcAddr("vkDestroyDebugUtilsMessengerEXT")
    );
}

RenderDebug::RenderDebug(vk::UniqueInstance &instance) {
    load_proxies(instance);

    auto severity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    auto type = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    vk::DebugUtilsMessengerCreateInfoEXT create_info(
        vk::DebugUtilsMessengerCreateFlagsEXT(),
        severity, type, &message_callback
    );
    messenger_ = instance->createDebugUtilsMessengerEXTUnique(
        create_info
    );
}