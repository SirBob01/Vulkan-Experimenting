#include "texture.h"

TextureData::TextureData(uint32_t width, 
                         uint32_t height,
                         uint32_t mip_levels,
                         vk::Device &logical,
                         PhysicalDevice &physical,
                         ImageMemoryAllocator &allocator,
                         RenderBuffer &staging_buffer,
                         vk::CommandPool &command_pool,
                         vk::Queue &queue) : physical_(physical), 
                                             allocator_(allocator) {
    logical_ = logical;
    properties_ = vk::MemoryPropertyFlagBits::eDeviceLocal;

    width_ = width;
    height_ = height;
    mip_levels_ = mip_levels;

    command_pool_ = command_pool;
    queue_ = queue;

    // Create the image
    image_ = create_image(
        logical_,
        width,
        height,
        mip_levels_,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferSrc |
        vk::ImageUsageFlagBits::eTransferDst | 
        vk::ImageUsageFlagBits::eSampled,
        vk::SampleCountFlagBits::e1
    );
    handle_ = allocator_.allocate_memory(image_.get());

    // Load image data
    transition_layout(
        vk::ImageLayout::eUndefined, 
        vk::ImageLayout::eTransferDstOptimal
    );
    copy_from_buffer(staging_buffer);
    generate_mipmaps();

    // Create the image view
    view_ = create_view(
        logical_,
        image_.get(),
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageAspectFlagBits::eColor,
        mip_levels_
    );
}

TextureData::~TextureData() {
    allocator_.remove_image(handle_);
}

void TextureData::transition_layout(vk::ImageLayout from, vk::ImageLayout to) {
    // Define the memory barrier
    vk::ImageMemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eNoneKHR;
    barrier.dstAccessMask = vk::AccessFlagBits::eNoneKHR;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = 0;
    barrier.dstQueueFamilyIndex = 0;
    barrier.image = image_.get();

    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    vk::PipelineStageFlags src_stage, dst_stage;
    if(from == vk::ImageLayout::eUndefined && 
       to == vk::ImageLayout::eTransferDstOptimal) {
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dst_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if(from == vk::ImageLayout::eTransferDstOptimal && 
            to == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eTransfer;
        dst_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else {
        throw std::runtime_error("Texture loading failed, layout transition is unsupported.");
    }

    // Allocate a new one-time command buffer for the transition
    vk::CommandBufferAllocateInfo cmd_alloc_info;
    cmd_alloc_info.commandPool = command_pool_;
    cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
    cmd_alloc_info.commandBufferCount = 1;

    vk::UniqueCommandBuffer command_buffer = std::move(
        logical_.allocateCommandBuffersUnique(cmd_alloc_info)[0]
    );

    // Execute the transition commands
    vk::CommandBufferBeginInfo cmd_begin_info;
    cmd_begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    command_buffer->begin(cmd_begin_info);
    command_buffer->pipelineBarrier(
        src_stage,
        dst_stage,
        vk::DependencyFlagBits::eByRegion, // TODO 
        nullptr, nullptr, 
        barrier
    );
    command_buffer->end();

    // Submit the command to the graphics queue
    vk::SubmitInfo submit_info;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer.get();

    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}

void TextureData::copy_from_buffer(RenderBuffer &buffer) {
    // Define the image copy region
    vk::BufferImageCopy copy_region;
    copy_region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;

    copy_region.imageExtent.width = width_;
    copy_region.imageExtent.height = height_;
    copy_region.imageExtent.depth = 1;

    // Allocate a new one-time command buffer for copying buffer data
    vk::CommandBufferAllocateInfo cmd_alloc_info;
    cmd_alloc_info.commandPool = command_pool_;
    cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
    cmd_alloc_info.commandBufferCount = 1;
    
    vk::UniqueCommandBuffer command_buffer = std::move(
        logical_.allocateCommandBuffersUnique(cmd_alloc_info)[0]
    );

    // Execute commands for copying buffer data
    vk::CommandBufferBeginInfo cmd_begin_info;
    cmd_begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    command_buffer->begin(cmd_begin_info);
    command_buffer->copyBufferToImage(
        buffer.get_handle(),
        image_.get(), 
        vk::ImageLayout::eTransferDstOptimal, 
        1, 
        &copy_region
    );
    command_buffer->end();

    // Submit to the queue
    vk::SubmitInfo submit_info;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer.get();

    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}

void TextureData::generate_mipmaps() {
    auto format_properties = physical_.get_format_properties(vk::Format::eR8G8B8A8Srgb);
    auto tiling_features = format_properties.optimalTilingFeatures;
    if(!(tiling_features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        // Cannot perform linear filtering to generate mipmaps
        // Just transition straight to shader readable layout
        transition_layout(
            vk::ImageLayout::eTransferDstOptimal, 
            vk::ImageLayout::eShaderReadOnlyOptimal
        );
        return;
    }
    
    vk::ImageMemoryBarrier barrier;
    barrier.srcQueueFamilyIndex = 0;
    barrier.dstQueueFamilyIndex = 0;
    barrier.image = image_.get();

    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Allocate a new one-time command buffer for copying buffer data
    vk::CommandBufferAllocateInfo cmd_alloc_info;
    cmd_alloc_info.commandPool = command_pool_;
    cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
    cmd_alloc_info.commandBufferCount = 1;
    
    vk::UniqueCommandBuffer command_buffer = std::move(
        logical_.allocateCommandBuffersUnique(cmd_alloc_info)[0]
    );

    // Execute commands for blitting the mipped textures
    vk::CommandBufferBeginInfo cmd_begin_info;
    cmd_begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    command_buffer->begin(cmd_begin_info);

    uint32_t mip_width = width_;
    uint32_t mip_height = height_;
    for(uint32_t i = 1; i < mip_levels_; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlagBits::eByRegion, 
            nullptr, nullptr, 
            barrier
        );

        // Blit the new mip
        vk::ImageBlit blit;
        blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
        blit.srcOffsets[1] = vk::Offset3D(
            mip_width, 
            mip_height, 
            1
        );
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
        blit.dstOffsets[1] = vk::Offset3D(
            mip_width > 1 ? mip_width / 2 : 1,
            mip_height > 1 ? mip_height / 2 : 1, 
            1
        );
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;


        command_buffer->blitImage(
            image_.get(), vk::ImageLayout::eTransferSrcOptimal,
            image_.get(), vk::ImageLayout::eTransferDstOptimal,
            blit,
            vk::Filter::eLinear
        );

        // Transition mip to shader readable format
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlagBits::eByRegion, 
            nullptr, nullptr, 
            barrier
        );

        if(mip_width > 1) mip_width /= 2;
        if(mip_height > 1) mip_height /= 2;
    }

    // Transition last mip to optimal shader readable layout
    barrier.subresourceRange.baseMipLevel = mip_levels_ - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    
    command_buffer->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::DependencyFlagBits::eByRegion, 
        nullptr, nullptr, 
        barrier
    );
    command_buffer->end();

    // Submit to the queue
    vk::SubmitInfo submit_info;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer.get();

    queue_.submit(submit_info, nullptr);
    queue_.waitIdle();
}

vk::Image &TextureData::get_image() {
    return image_.get();
}

vk::ImageView &TextureData::get_view() {
    return view_.get();
}
