#include "texture.h"

Texture::Texture(vk::Device &logical,
                 PhysicalDevice &physical,
                 vk::CommandPool &command_pool,
                 vk::Queue &queue,
                 RenderBuffer &texels, 
                 uint32_t width, uint32_t height) : physical_(physical) {
    logical_ = logical;
    usage_ = vk::ImageUsageFlagBits::eTransferDst | 
             vk::ImageUsageFlagBits::eSampled;
    properties_ = vk::MemoryPropertyFlagBits::eDeviceLocal;

    width_ = width;
    height_ = height;

    command_pool_ = command_pool;
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
        usage_,
        vk::SharingMode::eExclusive
    );
    image_ = logical_.createImageUnique(image_info);

    // Load image data
    alloc_memory();
    transition_layout(
        vk::ImageLayout::eUndefined, 
        vk::ImageLayout::eTransferDstOptimal
    );
    copy_from_buffer(texels);
    transition_layout(
        vk::ImageLayout::eTransferDstOptimal, 
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
    create_view();
}

Texture::~Texture() {
    image_.reset();
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
        throw std::runtime_error("Vulkan failed to allocate memory for a texture.");
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
        vk::AccessFlagBits::eNoneKHR,
        vk::AccessFlagBits::eNoneKHR,
        from, to,
        0, 0,
        image_.get(),
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    );
    vk::PipelineStageFlags src_stage, dst_stage;
    if(from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eTransferDstOptimal) {
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dst_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if(from == vk::ImageLayout::eTransferDstOptimal && to == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eTransfer;
        dst_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else {
        throw std::runtime_error("Texture loading failed, layout transition is unsupported.");
    }

    vk::CommandBufferAllocateInfo alloc_info(
        command_pool_,
        vk::CommandBufferLevel::ePrimary,
        1
    );
    vk::UniqueCommandBuffer command_buffer = std::move(
        logical_.allocateCommandBuffersUnique(alloc_info)[0]
    );

    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );
    command_buffer->begin(begin_info);
    command_buffer->pipelineBarrier(
        src_stage,
        dst_stage,
        vk::DependencyFlagBits::eByRegion, // TODO 
        nullptr, nullptr, 
        barrier
    );
    command_buffer->end();

    vk::SubmitInfo submit_info(
        0, nullptr, nullptr, 
        1, &command_buffer.get()
    );
    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}

void Texture::copy_from_buffer(RenderBuffer &buffer) {
    vk::CommandBufferAllocateInfo alloc_info(
        command_pool_,
        vk::CommandBufferLevel::ePrimary,
        1
    );
    vk::UniqueCommandBuffer command_buffer = std::move(
        logical_.allocateCommandBuffersUnique(alloc_info)[0]
    );

    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );
    command_buffer->begin(begin_info);
    vk::BufferImageCopy region(
        0, 0, 0,
        { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
        {0, 0, 0},
        {width_, height_, 1}  
    );
    command_buffer->copyBufferToImage(
        buffer.get_handle(),
        image_.get(), 
        vk::ImageLayout::eTransferDstOptimal, 
        1, 
        &region
    );
    command_buffer->end();

    vk::SubmitInfo submit_info(
        0, nullptr, nullptr, 
        1, &command_buffer.get()
    );
    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}

void Texture::create_view() {
    vk::ImageViewCreateInfo view_info(
        vk::ImageViewCreateFlags(),
        image_.get(),
        vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Srgb
    );
    view_info.subresourceRange = {
        vk::ImageAspectFlagBits::eColor,
        0, 1, 0, 1
    };
    view_ = logical_.createImageViewUnique(view_info);
}

vk::Image &Texture::get_image() {
    return image_.get();    
}

vk::ImageView &Texture::get_view() {
    return view_.get();
}