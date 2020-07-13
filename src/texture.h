#ifndef RENDER_TEXTURE_H_
#define RENDER_TEXTURE_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION

#include <vulkan/vulkan.hpp>

#include "renderbuffer.h"
#include "physical.h"

// Holds texture data
class Texture {
    vk::Device logical_;
    PhysicalDevice &physical_;

    vk::UniqueImage image_;
    vk::UniqueDeviceMemory memory_;

    vk::ImageUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;

    // Allocate device memory and bind to image
    void alloc_memory();

public:
    Texture(vk::Device &logical, PhysicalDevice &physical,
            RenderBuffer &texels, uint32_t width, uint32_t height);
};

#endif