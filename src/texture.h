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
    vk::UniqueImageView view_;
    vk::UniqueDeviceMemory memory_;

    vk::ImageUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;

    uint32_t width_; 
    uint32_t height_;

    vk::CommandPool command_pool_;
    vk::Queue queue_;

    // Allocate device memory and bind to image
    void alloc_memory();

    // Transition the image layout
    void transition_layout(vk::ImageLayout from, vk::ImageLayout to);

    // Copy texel data from the buffer
    void copy_from_buffer(RenderBuffer &buffer);

    // Create an image view
    void create_view();

public:
    Texture(vk::Device &logical, PhysicalDevice &physical,
            vk::CommandPool &command_pool,
            vk::Queue &queue,
            RenderBuffer &texels, uint32_t width, uint32_t height);
    ~Texture();

    // Get the image this texture refers to
    vk::Image &get_image();

    // Get the image view of this texture
    vk::ImageView &get_view();
};

#endif