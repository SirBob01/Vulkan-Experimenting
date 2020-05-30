#include "renderbuffer.h"

RenderBuffer::RenderBuffer(vk::Device &logical, size_t length, 
                           vk::BufferUsageFlags usage, 
                           vk::MemoryPropertyFlags properties,
                           vk::PhysicalDeviceMemoryProperties device_spec) {
    logical_ = logical;
    length_ = length;
    usage_ = usage;
    properties_ = properties;
    device_spec_ = device_spec;

    host_visible_ = static_cast<bool>(
        properties_ & vk::MemoryPropertyFlagBits::eHostVisible
    );

    initialize_buffer();
    alloc_memory();

    if(host_visible_) {
        bind_ = reinterpret_cast<char *>(logical_.mapMemory(
            memory_.get(), 0, 
            length_
        ));
    }
}

RenderBuffer::~RenderBuffer() {
    if(host_visible_) {
        logical_.unmapMemory(memory_.get());
    }
}

void RenderBuffer::initialize_buffer() {
    vk::BufferCreateInfo buffer_info(
        vk::BufferCreateFlags(),
        length_, usage_
    );
    handle_ = logical_.createBufferUnique(buffer_info);
}

void RenderBuffer::alloc_memory() {
    auto requirements = logical_.getBufferMemoryRequirements(
        handle_.get()
    );

    int memory_type = -1;
    for(uint32_t i = 0; i < device_spec_.memoryTypeCount; i++) {
        if((requirements.memoryTypeBits & (1 << i)) && 
           (device_spec_.memoryTypes[i].propertyFlags & properties_)) {
            memory_type = i;
            break;
        }
    }
    if(memory_type < 0) {
        throw std::runtime_error("Vulkan failed to create buffer");
    }
    vk::MemoryAllocateInfo alloc_info(
        requirements.size,
        memory_type
    );
    memory_ = logical_.allocateMemoryUnique(
        alloc_info
    );    
    // Bind the device memory to the buffer
    logical_.bindBufferMemory(
        handle_.get(), memory_.get(), 0
    );
}

void RenderBuffer::check_subbuffer(SubBuffer buffer) {
    if(buffer >= subbuffers_.size()) {
        throw std::runtime_error("Invalid subbuffer index");
    }
}

size_t RenderBuffer::get_length() {
    return length_;
}

vk::Buffer &RenderBuffer::get_handle() {
    return handle_.get();
}

int RenderBuffer::get_subbuffer_count() {
    return subbuffers_.size();
}

size_t RenderBuffer::get_offset(SubBuffer buffer) {
    check_subbuffer(buffer);
    return subbuffers_[buffer].offset;
}

size_t RenderBuffer::get_subfill(SubBuffer buffer) {
    check_subbuffer(buffer);
    return subbuffers_[buffer].filled;
}

size_t RenderBuffer::get_subsize(SubBuffer buffer) {
    check_subbuffer(buffer);
    return subbuffers_[buffer].size;
}

SubBuffer RenderBuffer::suballoc(size_t size) {
    if(subbuffers_.empty()) {
        subbuffers_.push_back({size, 0, 0});
    }
    else {
        auto &last = subbuffers_.back();
        subbuffers_.push_back({
            size, last.offset + last.size, 0
        });
    }
    return subbuffers_.size() - 1;
}

void RenderBuffer::resuballoc(SubBuffer buffer, size_t size) {
    // TODO
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];
    size_t shift = size - buffer_data.size;
    size_t total = 0;
    // Only shift from index buffer+1 onward
    // Algorithm:
    // 1. Copy all data from subbuffers_[buffer+1] into a temporary buffer
    // 2. Adjust subbuffers_[i].offset += shift
    // 3. Refill buffer starting from subbuffers_[buffer+1].offset
    for(int i = buffer+1; i < subbuffers_.size(); i++) {
        subbuffers_[i].offset += shift;
    }
}

void RenderBuffer::clear(SubBuffer buffer) {
    check_subbuffer(buffer);
    subbuffers_[buffer].filled = 0;
}

void RenderBuffer::copy(SubBuffer buffer, void *data, int length) {
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible");
    }
    if(length+buffer_data.filled > buffer_data.size) {
        // TODO
        //resuballoc(buffer, length+buffer_data.filled);
    }
    std::memcpy(bind_ + buffer_data.offset, data, length);
    buffer_data.filled += length;
}

void RenderBuffer::copy_raw(void *data, int length, int offset) {
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible");
    }
    if(length > length_) {
        throw std::runtime_error("Copied data larger than buffer size");
    }
    std::memcpy(bind_ + offset, data, length);
}