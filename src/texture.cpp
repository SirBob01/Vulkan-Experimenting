#include "texture.h"

Texture::Texture(vk::Device &logical,
                 PhysicalDevice &physical,
                 vk::CommandBuffer &command_buffer,
                 vk::Queue &queue,
                 RenderBuffer &texels, 
                 uint32_t width, uint32_t height) : physical_(physical) {
    logical_ = logical;
    usage_ = vk::ImageUsageFlagBits::eTransferDst | 
             vk::ImageUsageFlagBits::eSampled;
    properties_ = vk::MemoryPropertyFlagBits::eDeviceLocal;

    command_buffer_ = command_buffer;
    queue_ = queue;

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
    transition_layout(
        vk::ImageLayout::eUndefined, 
        vk::ImageLayout::eTransferDstOptimal
    );
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

void Texture::transition_layout(vk::ImageLayout from, vk::ImageLayout to) {
    vk::ImageMemoryBarrier barrier(
        vk::AccessFlagBits::eVertexAttributeRead, // TODO src
        vk::AccessFlagBits::eVertexAttributeRead, // TODO dst
        from, to,
        0, 0,
        image_.get(),
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    );

    // TODO: Abstract away command buffer stuff
    // I'm beginning to repeat myself (see RenderBuffer copy)
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );
    command_buffer_.begin(begin_info);
    command_buffer_.pipelineBarrier(
        vk::PipelineStageFlagBits::eDrawIndirect, // TODO
        vk::PipelineStageFlagBits::eDrawIndirect, // TODO
        vk::DependencyFlagBits::eByRegion,        // TODO 
        nullptr, nullptr, 
        barrier
    );
    command_buffer_.end();

    // Fuck, there has to be a good abstraction for this stuff...
    vk::SubmitInfo submit_info(
        0, nullptr, nullptr, 
        1, &command_buffer_
    );
    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}