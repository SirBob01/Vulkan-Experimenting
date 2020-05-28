#include "renderbuffer.h"

// Initialize the buffer handle
void RenderBuffer::initialize_buffer(vk::BufferUsageFlags usage) {
    vk::BufferCreateInfo buffer_info(
        vk::BufferCreateFlags(),
        length_, usage
    );
    handle_ = logical_.createBufferUnique(buffer_info);
}

// Allocate memory in the GPU for the buffer
void RenderBuffer::alloc_memory(vk::MemoryPropertyFlags properties, 
                                vk::PhysicalDeviceMemoryProperties available) {
    auto requirements = logical_.getBufferMemoryRequirements(
        handle_.get()
    );

    int memory_type = -1;
    for(uint32_t i = 0; i < available.memoryTypeCount; i++) {
        if((requirements.memoryTypeBits & (1 << i)) && 
           (available.memoryTypes[i].propertyFlags & properties)) {
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
}

RenderBuffer::RenderBuffer(vk::UniqueDevice &logical, size_t length, 
                           vk::BufferUsageFlags usage, 
                           vk::MemoryPropertyFlags properties,
                           vk::PhysicalDeviceMemoryProperties device_spec) {
    logical_ = logical.get();
    length_ = length;
    host_visible_ = static_cast<bool>(
        properties & vk::MemoryPropertyFlagBits::eHostVisible
    );

    initialize_buffer(usage);
    alloc_memory(properties, device_spec);

    // Bind the device memory to the buffer
    logical_.bindBufferMemory(
        handle_.get(), memory_.get(), 0
    );

    // Map and unmap ONLY once within buffer lifetime
    if(host_visible_) {
        bind_ = logical_.mapMemory(
            memory_.get(), 0, 
            length_
        );
    }
}

RenderBuffer::~RenderBuffer() {
    if(host_visible_) {
        logical_.unmapMemory(memory_.get());
    }
}

// Get the length of the buffer
size_t RenderBuffer::get_length() {
    return length_;
}

// Get the handle to the Vulkan buffer
vk::Buffer &RenderBuffer::get_handle() {
    return handle_.get();
}

// Copy the data to the buffer
void RenderBuffer::copy(void *usr_data, int length) {
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible");
    }
    if(length > length_) {
        throw std::runtime_error("Copied data larger than buffer size");
    }
    memcpy(bind_, usr_data, length);
}