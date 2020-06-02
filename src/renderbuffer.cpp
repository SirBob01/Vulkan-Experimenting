#include "renderbuffer.h"

RenderBuffer::RenderBuffer(size_t length, 
                           vk::Device &logical, 
                           PhysicalDevice &physical,
                           vk::BufferUsageFlags usage, 
                           vk::MemoryPropertyFlags properties,
                           vk::CommandBuffer &copier, 
                           vk::Queue &transfer_queue) : physical_(physical) {
    logical_ = logical;
    length_ = length;

    // General case? We're gonna be doing a lot of copying
    usage_ = usage | 
             vk::BufferUsageFlagBits::eTransferSrc | 
             vk::BufferUsageFlagBits::eTransferDst;
    properties_ = properties;

    copier_ = copier;
    transfer_queue_ = transfer_queue;

    host_visible_ = static_cast<bool>(
        properties_ & vk::MemoryPropertyFlagBits::eHostVisible
    );

    // Subbuffer offsets must be a multiple of 4
    offset_alignment_ = 4;

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
    handle_.reset();
    memory_.reset();
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

void RenderBuffer::copy_to_offset(RenderBuffer &target, size_t length,
                                  size_t src_offset, size_t dst_offset) {
    if(length + src_offset > length_) {
        throw std::runtime_error("SubBuffer copy length too large");   
    }
    if(length + dst_offset > target.length_) {
        target.resize(length + dst_offset);
    }
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );
    // Perform the copy
    copier_.begin(begin_info);
    vk::BufferCopy copy_region(src_offset, dst_offset, length);
    copier_.copyBuffer(
        get_handle(), target.get_handle(), 
        1, &copy_region
    );
    copier_.end();

    // Submit the command to the graphics queue
    vk::SubmitInfo submit_info(
        0, nullptr, nullptr, 
        1, &copier_
    );
    transfer_queue_.submit(submit_info, nullptr);
    transfer_queue_.waitIdle();
}

void RenderBuffer::resize(size_t size) {
    // Copy data to temporary buffer
    RenderBuffer temp(
        length_, logical_, physical_, usage_, 
        properties_, copier_, transfer_queue_
    );
    copy_to_offset(temp, length_, 0, 0);
    if(host_visible_) {
        logical_.unmapMemory(memory_.get());
    }

    // Reinitialize buffer to new size
    length_ = round_up(size, offset_alignment_);
    initialize_buffer();
    alloc_memory();
    if(host_visible_) {
        bind_ = reinterpret_cast<char *>(logical_.mapMemory(
            memory_.get(), 0, 
            length_
        ));
    }
    // Copy data back
    temp.copy_to_offset(*this, temp.get_size(), 0, 0);
}

void RenderBuffer::resuballoc(SubBuffer buffer, size_t size) {
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];

    size = round_up(size, offset_alignment_);
    size_t shift = size - buffer_data.size;
    size_t shift_length = 0;

    // Check for realloc
    auto last = subbuffers_.back();
    size_t new_size = last.offset + last.size + shift;
    if(new_size > length_) {
        resize(new_size);
    }

    // Perform the copying
    buffer_data.size = size;
    for(int i = buffer+1; i < subbuffers_.size(); i++) {
        shift_length += subbuffers_[i].size;
        subbuffers_[i].offset += shift;
    }
    if(shift_length) {
        RenderBuffer temp(
            shift_length, logical_, physical_, usage_, 
            properties_, copier_, transfer_queue_
        );
        copy_to_offset(
            temp, 
            shift_length, 
            subbuffers_[buffer+1].offset - shift, 
            0
        );
        temp.copy_to_offset(
            *this, 
            shift_length, 
            0,
            subbuffers_[buffer+1].offset 
        );
    }
}

void RenderBuffer::check_subbuffer(SubBuffer buffer) {
    if(buffer >= subbuffers_.size()) {
        throw std::runtime_error("Invalid subbuffer index");
    }
}

size_t RenderBuffer::get_size() {
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

char *RenderBuffer::get_mapped() {
    if(host_visible_) {
        return bind_;
    }
    return nullptr;
}

SubBuffer RenderBuffer::suballoc(size_t size) {
    size = round_up(size, offset_alignment_);
    if(subbuffers_.empty()) {
        subbuffers_.push_back({size, 0, 0});
        if(size > length_) {
            resize(size);
        }
    }
    else {
        auto last = subbuffers_.back();
        size_t offset = last.offset + last.size;
        subbuffers_.push_back({
            size, offset, 0
        });
        if(offset + size > length_) {
            resize(offset + size);
        }
    }
    return subbuffers_.size() - 1;
}

void RenderBuffer::clear(SubBuffer buffer) {
    check_subbuffer(buffer);
    subbuffers_[buffer].filled = 0;
}

void RenderBuffer::copy(SubBuffer buffer, void *data, size_t length) {
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible");
    }
    if(length + buffer_data.filled > buffer_data.size) {
        resuballoc(buffer, length + buffer_data.filled);
    }
    std::memcpy(
        bind_ + buffer_data.offset + buffer_data.filled, 
        data, 
        length
    );
    buffer_data.filled += length;
}

void RenderBuffer::copy_buffer(RenderBuffer &target, size_t length,
                               SubBuffer src, SubBuffer dst) {
    check_subbuffer(src);
    target.check_subbuffer(dst);

    auto &src_buffer = subbuffers_[src];
    auto &dst_buffer = target.subbuffers_[dst];
    
    if(length + dst_buffer.filled > dst_buffer.size) {
        target.resuballoc(dst, length + dst_buffer.filled);
    }
    copy_to_offset(
        target, length, 
        src_buffer.offset, 
        dst_buffer.offset + dst_buffer.filled
    );
    dst_buffer.filled += length;
}

void RenderBuffer::copy_raw(void *data, size_t length, size_t offset) {
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible");
    }
    if(offset + length > length_) {
        resize(offset + length);
    }
    std::memcpy(bind_ + offset, data, length);
}