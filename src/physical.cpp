#include "physical.h"

PhysicalDevice::PhysicalDevice(vk::PhysicalDevice handle, vk::SurfaceKHR surface) {
    handle_ = handle;
    surface_ = surface;

    properties_ = handle_.getProperties();
    memory_ = handle_.getMemoryProperties();
    features_ = handle_.getFeatures();

    // Add the extensions required by all devices
    extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    get_command_queues();
    get_swapchain_support();
} 

bool PhysicalDevice::is_complete() {
    bool graphics = queues_.graphics.count;
    bool presentation = queues_.present.count;
    bool transfer = queues_.transfer.count;
    return graphics && presentation && transfer;
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
            queues_.present.index = i;
            queues_.present.count = family.queueCount;
        }
        
        if(family.queueFlags & vk::QueueFlagBits::eGraphics) {
            // Dedicated graphics_queue
            queues_.graphics.index = i;
            queues_.graphics.count = family.queueCount;
        }
        else if(family.queueFlags & vk::QueueFlagBits::eTransfer) {
            // Dedicated transfer queue
            queues_.transfer.index = i;
            queues_.transfer.count = family.queueCount;
        }
        if(is_complete()) {
            break;
        }
        i++;
    }
    // Use same queue if no available dedicated
    if(!queues_.graphics.count) {
        queues_.graphics = queues_.present;
    }
    if(!queues_.transfer.count) {
        queues_.transfer = queues_.present;
    }
}

vk::PhysicalDevice PhysicalDevice::get_handle() {
    return handle_;
}  

std::string PhysicalDevice::get_name() {
    return properties_.deviceName;
}

AvailableQueues &PhysicalDevice::get_available_queues() {
    return queues_;
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

vk::PhysicalDeviceMemoryProperties PhysicalDevice::get_memory() {
    return memory_;
}

vk::PhysicalDeviceLimits PhysicalDevice::get_limits() {
    return properties_.limits;
}

int PhysicalDevice::get_score() {
    int score = 0;

    // Ensure all necessary features are present
    if(!is_complete() ||
       !is_supporting_extensions() ||
       !is_supporting_swapchain() ||
       !features_.geometryShader) {
        return 0;
    }

    // Dedicated GPU are prioritized
    auto discrete = vk::PhysicalDeviceType::eDiscreteGpu;
    if(properties_.deviceType == discrete) {
        score += 1000;
    }

    // How much can be stored in VRAM?
    score += properties_.limits.maxImageDimension2D;
    return score;
}