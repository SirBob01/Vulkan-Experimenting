#include "texture.h"

TextureData::TextureData(vk::Device &logical, 
                         PhysicalDevice &physical,
                         vk::CommandPool &command_pool,
                         vk::Queue &queue,
                         RenderBuffer &staging_buffer,
                         uint32_t width, 
                         uint32_t height,
                         uint32_t mip_levels) : physical_(physical) {
    logical_ = logical;
    properties_ = vk::MemoryPropertyFlagBits::eDeviceLocal;

    width_ = width;
    height_ = height;
    mip_levels_ = mip_levels;

    command_pool_ = command_pool;
    queue_ = queue;

    // Create the image
    vk::ImageCreateInfo image_info;
    image_info.imageType = vk::ImageType::e2D;
    image_info.format = vk::Format::eR8G8B8A8Srgb;

    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;

    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = 1;
    image_info.samples = vk::SampleCountFlagBits::e1;
    image_info.tiling = vk::ImageTiling::eOptimal;
    image_info.usage = vk::ImageUsageFlagBits::eTransferSrc |
                       vk::ImageUsageFlagBits::eTransferDst | 
                       vk::ImageUsageFlagBits::eSampled;
    image_info.sharingMode = vk::SharingMode::eExclusive;

    image_ = logical_.createImageUnique(image_info);

    // Load image data
    alloc_memory();
    transition_layout(
        vk::ImageLayout::eUndefined, 
        vk::ImageLayout::eTransferDstOptimal
    );
    copy_from_buffer(staging_buffer);
    generate_mipmaps();
    create_view();
}

TextureData::~TextureData() {
    image_.reset();
}

void TextureData::alloc_memory() {
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

    // Allocate memory
    vk::MemoryAllocateInfo mem_alloc_info;
    mem_alloc_info.allocationSize = requirements.size;
    mem_alloc_info.memoryTypeIndex = memory_type;

    memory_ = logical_.allocateMemoryUnique(
        mem_alloc_info
    );
    
    // Bind the device memory to the image
    logical_.bindImageMemory(
        image_.get(), memory_.get(), 0
    );
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

void TextureData::create_view() {
    vk::ImageViewCreateInfo view_info;
    view_info.image = image_.get();
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = vk::Format::eR8G8B8A8Srgb;

    view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    view_ = logical_.createImageViewUnique(view_info);
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
