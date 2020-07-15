#ifndef RENDER_TEXTURE_H_
#define RENDER_TEXTURE_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION

#include <vulkan/vulkan.hpp>

#include "buffer.h"
#include "physical.h"

// Holds texture data
class Texture {
    vk::Device logical_;
    PhysicalDevice &physical_;

    vk::UniqueImage image_;
    vk::UniqueDeviceMemory memory_;

    vk::ImageUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;

    vk::CommandBuffer command_buffer_;
    vk::Queue queue_;

    // Allocate device memory and bind to image
    void alloc_memory();

    // Transition the image layout
    void transition_layout(vk::ImageLayout from, vk::ImageLayout to);

public:
    Texture(vk::Device &logical, PhysicalDevice &physical, 
            vk::CommandBuffer &command_buffer,
            vk::Queue &queue,
            RenderBuffer &texels, uint32_t width, uint32_t height);
};

#endif