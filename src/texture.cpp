#include "texture.h"

Texture::Texture(vk::Device &logical,
                 PhysicalDevice &physical,
                 RenderBuffer &texels, 
                 uint32_t width, uint32_t height) : physical_(physical) {
    logical_ = logical;
    usage_ = vk::ImageUsageFlagBits::eTransferDst | 
             vk::ImageUsageFlagBits::eSampled;
    properties_ = vk::MemoryPropertyFlagBits::eDeviceLocal;

    // Create the image
    vk::ImageCreateInfo image_info(
        vk::ImageCreateFlags(),
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Srgb,
        {width, height, 1},
        1, // Mip levels
        1, // Array layers
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        usage_
    );
    image_ = logical_.createImageUnique(image_info);

    alloc_memory();
}

void Texture::alloc_memory() {
    auto requirements = logical_.getImageMemoryRequirements(
        image_.get()
    );
    auto device_spec = physical_.get_memory();
    int memory_type = -1;
    for(uint32_t i = 0; i < device_spec.memoryTypeCount; i++) {
        if((requirements.memoryTypeBits & (1 << i)) && 
           (device_spec.memoryTypes[i].propertyFlags & properties_)) {
            memory_type = i;
            break;
        }
    }
    if(memory_type < 0) {
        throw std::runtime_error("Vulkan failed to create texture.");
    }
    vk::MemoryAllocateInfo alloc_info(
        requirements.size,
        memory_type
    );
    memory_ = logical_.allocateMemoryUnique(
        alloc_info
    );
    logical_.bindImageMemory(
        image_.get(), memory_.get(), 0
    );
}