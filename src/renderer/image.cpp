#include "image.h"
#include <iostream>
vk::UniqueImage create_image(vk::Device &logical,
                             uint32_t width,
                             uint32_t height,
                             uint32_t mip_levels,
                             vk::Format format,
                             vk::ImageTiling tiling, 
                             vk::Flags<vk::ImageUsageFlagBits> usage,
                             vk::SampleCountFlagBits samples) {
    vk::ImageCreateInfo image_info;
    image_info.imageType = vk::ImageType::e2D;
    image_info.format = format;

    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;

    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = samples;

    image_info.tiling = tiling;
    image_info.usage = usage;
    image_info.sharingMode = vk::SharingMode::eExclusive;

    return logical.createImageUnique(image_info);
}

vk::UniqueImageView create_view(vk::Device &logical,
                                vk::Image &image,
                                vk::Format format, 
                                vk::Flags<vk::ImageAspectFlagBits> aspect_mask, 
                                uint32_t mip_levels) {
    vk::ImageViewCreateInfo view_info;
    view_info.image = image;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = format;

    view_info.subresourceRange.aspectMask = aspect_mask;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    return logical.createImageViewUnique(view_info);
}

bool MemoryMeta::operator==(const MemoryMeta &other) const {
    return memory_type == other.memory_type && 
           alignment == other.alignment;
}

std::size_t std::hash<MemoryMeta>::operator()(MemoryMeta const &image_memory) const {
    return ((image_memory.memory_type ^ (image_memory.alignment << 1)) >> 1);
}

ImagePool::ImagePool(vk::Device &logical,
                     MemoryMeta &memory_meta) {
    logical_ = logical;
    capacity_ = 1024 * 1024 * 256; // 256M
    alignment_ = memory_meta.alignment;

    vk::MemoryAllocateInfo mem_alloc_info;
    mem_alloc_info.allocationSize = capacity_;
    mem_alloc_info.memoryTypeIndex = memory_meta.memory_type;

    memory_ = logical_.allocateMemoryUnique(
        mem_alloc_info
    );
}

int ImagePool::suballoc(vk::Image &image) {
    // Find an existing compatible subbuffer
    auto requirements = logical_.getImageMemoryRequirements(image);
    for(auto &index : recycle_) {
        if(subbuffers_[index].size >= requirements.size) {
            logical_.bindImageMemory(
                image, 
                memory_.get(), 
                subbuffers_[index].offset
            );
            recycle_.erase(index);
            return index;
        }
    }
    
    // Create a new subbuffer
    Subbuffer subbuffer;
    subbuffer.size = round_up(requirements.size, alignment_);
    subbuffer.filled = 0;

    if(subbuffers_.empty()) {
        subbuffer.offset = 0;
    }
    else {
        Subbuffer &last = subbuffers_.back();
        subbuffer.offset = last.offset + last.size;
    }

    // No more space!
    if(subbuffer.offset + subbuffer.size > capacity_) {
        return -1;
    }
    logical_.bindImageMemory(
        image, 
        memory_.get(), 
        subbuffer.offset
    );
    subbuffers_.push_back(subbuffer);
    return subbuffers_.size() - 1;
}

void ImagePool::remove(int index) {
    recycle_.insert(index);
}

ImageMemoryAllocator::ImageMemoryAllocator(vk::Device &logical,
                                           PhysicalDevice &physical) : physical_(physical) {
    logical_ = logical;
}

MemoryMeta ImageMemoryAllocator::get_memory_meta(vk::Image &image) {
    auto requirements = logical_.getImageMemoryRequirements(image);
    auto device_spec = physical_.get_memory();
    auto properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    int memory_type = -1;
    for(uint32_t i = 0; i < device_spec.memoryTypeCount; i++) {
        if((requirements.memoryTypeBits & (1 << i)) && 
           (device_spec.memoryTypes[i].propertyFlags & properties)) {
            memory_type = i;
            break;
        }
    }
    if(memory_type < 0) {
        throw std::runtime_error("Vulkan failed to allocate memory for an image.");
    }

    MemoryMeta memory_meta;
    memory_meta.memory_type = memory_type;
    memory_meta.alignment = requirements.alignment;
    return memory_meta;
}

ImageMemoryHandle ImageMemoryAllocator::allocate_memory(vk::Image &image) {
    MemoryMeta memory_meta = get_memory_meta(image);
    std::vector<std::unique_ptr<ImagePool>> &pools = memory_[memory_meta];
    
    // Find a compatible pool with available space and bind the image
    int index = -1;
    int pool_index;
    for(pool_index = 0; pool_index < pools.size(); pool_index++) {
        index = pools[pool_index]->suballoc(image);
        if(index >= 0) {
            break;
        }
    }

    // If none of the pools are available, create a new one
    if(index < 0) {
        pools.push_back(
            std::make_unique<ImagePool>(logical_, memory_meta)
        );
        pool_index = pools.size() - 1;
        index = pools.back()->suballoc(image);
    }

    ImageMemoryHandle handle;
    handle.memory_meta = memory_meta;
    handle.pool = pool_index;
    handle.index = index;
    return handle;
}

void ImageMemoryAllocator::remove_image(ImageMemoryHandle handle) {
    memory_[handle.memory_meta][handle.pool]->remove(handle.index);
}

void ImageMemoryAllocator::reset() {
    memory_.clear();
}