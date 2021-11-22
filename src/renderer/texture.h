#ifndef RENDER_TEXTURE_H_
#define RENDER_TEXTURE_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION

#include <vulkan/vulkan.hpp>

#include "image.h"
#include "buffer.h"
#include "physical.h"

// A unique handle to an existing texture
using Texture = int;

// Texture image data used as a descriptor
class TextureData {
    vk::Device logical_;
    PhysicalDevice &physical_;

    vk::UniqueImage image_;
    vk::UniqueImageView view_;

    vk::MemoryPropertyFlags properties_;
    
    // Image memory
    ImageMemoryAllocator &allocator_;
    ImageMemoryHandle handle_;

    uint32_t width_; 
    uint32_t height_;
    uint32_t mip_levels_;

    vk::CommandPool command_pool_;
    vk::Queue queue_;

    // Transition the image layout
    void transition_layout(vk::ImageLayout from, vk::ImageLayout to);

    // Copy texel data from the buffer
    void copy_from_buffer(RenderBuffer &buffer);

    // Generate the various mipmap levels for the texture
    void generate_mipmaps();

public:
    TextureData(uint32_t width, 
                uint32_t height,
                uint32_t mip_levels,
                vk::Device &logical,
                PhysicalDevice &physical,
                ImageMemoryAllocator &allocator,
                RenderBuffer &staging_buffer,
                vk::CommandPool &command_pool,
                vk::Queue &queue);
    ~TextureData();

    // Get the image this texture refers to
    vk::Image &get_image();

    // Get the image view of this texture
    vk::ImageView &get_view();
};

#endif