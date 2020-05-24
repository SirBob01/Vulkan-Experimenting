#include "physical.h"

PhysicalDevice::PhysicalDevice(vk::PhysicalDevice handle, vk::SurfaceKHR surface) {
    handle_ = handle;
    surface_ = surface;

    // Add the extensions required by all devices
    extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    get_command_queues();
    get_swapchain_support();
} 

bool PhysicalDevice::is_complete() {
    bool graphics = queue_families_.graphics >= 0;
    bool presentation = queue_families_.present >= 0;
    return graphics && presentation;
}

bool PhysicalDevice::is_supporting_extensions() {
    auto available = handle_.enumerateDeviceExtensionProperties();
    std::set<std::string> required(
        extensions_.begin(),
        extensions_.end()
    );

    for(auto &extension : available) {
        required.erase(extension.extensionName);
    }
    return required.empty();
}

bool PhysicalDevice::is_supporting_swapchain() {
    bool formats = swapchain_.formats.empty();
    bool presents = swapchain_.presents.empty();
    return !formats && !presents;
}

void PhysicalDevice::get_command_queues() {
    auto families = handle_.getQueueFamilyProperties();
    int i = 0;

    for(auto &family : families) {
        bool present_support = handle_.getSurfaceSupportKHR(i, surface_);
        if(present_support) {
            queue_families_.present = i;
        }
        if(family.queueFlags & vk::QueueFlagBits::eGraphics) {
            queue_families_.graphics = i;
        }
        if(is_complete()) {
            break;
        }
        i++;
    }
}

vk::PhysicalDevice PhysicalDevice::get_handle() {
    return handle_;
}  

QueueFamilyIndices &PhysicalDevice::get_queue_families() {
    return queue_families_;
}

SwapchainSupport &PhysicalDevice::get_swapchain_support() {
    swapchain_ = {
        handle_.getSurfaceCapabilitiesKHR(surface_),
        handle_.getSurfaceFormatsKHR(surface_),
        handle_.getSurfacePresentModesKHR(surface_)
    };
    return swapchain_;
}

const std::vector<const char *> &PhysicalDevice::get_extensions() {
    return extensions_;
}

int PhysicalDevice::get_score() {
    auto properties = handle_.getProperties();
    auto features = handle_.getFeatures();
    int score = 0;

    // Ensure all necessary features are present
    if(!is_complete() ||
       !is_supporting_extensions() ||
       !is_supporting_swapchain() ||
       !features.geometryShader) {
        return 0;
    }

    // Dedicated GPU are prioritized
    auto discrete = vk::PhysicalDeviceType::eDiscreteGpu;
    if(properties.deviceType == discrete) {
        score += 1000;
    }

    // How much can be stored in VRAM?
    score += properties.limits.maxImageDimension2D;
    return score;
}