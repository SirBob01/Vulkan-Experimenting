#include <vulkan/vulkan.hpp>

#include <vector>
#include <set>

struct QueueFamilyIndices {
    int graphics = -1; // Graphics commands
    int present = -1;  // Presentation commands
};

struct SwapchainSupport {
    vk::SurfaceCapabilitiesKHR capabilities;   // Surface capabilities
    std::vector<vk::SurfaceFormatKHR> formats; // Pixel format, colorspace
    std::vector<vk::PresentModeKHR> presents;  // Presentation modes
};

class PhysicalDevice {
    vk::PhysicalDevice handle_;
    vk::SurfaceKHR surface_;

    SwapchainSupport swapchain_;
    QueueFamilyIndices queue_families_; 
    
    std::vector<const char *> extensions_;

    // Test if the device contains the required queue families
    bool is_complete();
    
    // Test if the device supports extensions
    bool is_supporting_extensions();

    // Test if the device supports swapchaining
    bool is_supporting_swapchain();
    
    // Query the available queue families
    void get_command_queues();

public:
    PhysicalDevice(vk::PhysicalDevice handle, vk::SurfaceKHR surface);
    
    // Grab the handle to the Vulkan physical device
    vk::PhysicalDevice get_handle();
        
    // Get the queue families
    QueueFamilyIndices &get_queue_families();

    // Get the swapchain options
    SwapchainSupport &get_swapchain_support();

    // Get all extensions available to the device
    const std::vector<const char *> &get_extensions();

    // Calculate the metric for GPU power
    int get_score();
};