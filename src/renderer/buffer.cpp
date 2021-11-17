#include "buffer.h"

RenderBuffer::RenderBuffer(size_t length, 
                           vk::Device &logical, 
                           PhysicalDevice &physical,
                           vk::BufferUsageFlags usage, 
                           vk::MemoryPropertyFlags properties,
                           vk::CommandBuffer &command_buffer, 
                           vk::CommandPool &command_pool,
                           vk::Queue &transfer_queue) : physical_(physical) {
    logical_ = logical;
    length_ = length;

    // General case? We're gonna be doing a lot of copying
    usage_ = usage | 
             vk::BufferUsageFlagBits::eTransferSrc | 
             vk::BufferUsageFlagBits::eTransferDst;
    properties_ = properties;

    command_buffer_ = command_buffer;
    command_pool_ = command_pool;
    transfer_queue_ = transfer_queue;

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
        throw std::runtime_error("Vulkan failed to create buffer.");
    }
    vk::MemoryAllocateInfo alloc_info(
        requirements.size,
        memory_type
    );

    // Suballocation offsets will be a multiple of this alignment
    offset_alignment_ = requirements.alignment;
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
        throw std::runtime_error("SubBuffer copy length too large.");   
    }
    if(length + dst_offset > target.length_) {
        target.resize(length + dst_offset);
    }
    logical_.resetCommandPool(
        command_pool_,
        vk::CommandPoolResetFlagBits::eReleaseResources
    );
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );
    // Perform the copy
    command_buffer_.begin(begin_info);
    vk::BufferCopy copy_region(src_offset, dst_offset, length);
    command_buffer_.copyBuffer(
        get_handle(), target.get_handle(), 
        copy_region
    );
    command_buffer_.end();

    // Submit the command to the transfer queue
    vk::SubmitInfo submit_info(
        0, nullptr, nullptr, 
        1, &command_buffer_
    );
    transfer_queue_.submit(submit_info, nullptr);
    transfer_queue_.waitIdle();
}

void RenderBuffer::resize(size_t size) {
    // Copy data to temporary buffer
    RenderBuffer temp(
        length_, logical_, physical_, usage_, 
        properties_, command_buffer_, command_pool_, transfer_queue_
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
        // TODO: Can we do this without allocating a temporary buffer?
        RenderBuffer temp(
            shift_length, logical_, physical_, usage_, 
            properties_, command_buffer_, command_pool_, transfer_queue_
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
    if(buffer >= subbuffers_.size() || recycle_.find(buffer) != recycle_.end()) {
        throw std::runtime_error("Invalid subbuffer index.");
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
    // Check if there are previously deleted subbuffers to be recycled
    if(recycle_.size()) {
        SubBuffer id = *recycle_.begin();
        recycle_.erase(id);
        return id;
    }

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

void RenderBuffer::copy(SubBuffer buffer, void *data, size_t length) {
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];
    if(!host_visible_) {
        throw std::runtime_error("Buffer is not host visible.");
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
        throw std::runtime_error("Buffer is not host visible.");
    }
    if(offset + length > length_) {
        resize(offset + length);
    }
    std::memcpy(bind_ + offset, data, length);
}

void RenderBuffer::remove(SubBuffer buffer, size_t offset, size_t length) {
    check_subbuffer(buffer);
    auto &buffer_data = subbuffers_[buffer];
    if(offset + length > buffer_data.filled) {
        throw std::runtime_error("Remove length longer than filled.");
    }
    size_t shift_start = offset + length;
    size_t shift_length = buffer_data.filled - shift_start;

    // Shift the data past the removal block to the left
    if(shift_length) {
        RenderBuffer temp(
            shift_length, logical_, physical_, usage_, 
            properties_, command_buffer_, command_pool_, transfer_queue_
        );
        copy_to_offset(
            temp, shift_length, 
            buffer_data.offset + shift_start, 0
        );
        temp.copy_to_offset(
            *this, shift_length, 
            0, buffer_data.offset + offset
        );    
    } 
    buffer_data.filled -= length;
}

void RenderBuffer::pop(SubBuffer buffer, size_t length) {
    check_subbuffer(buffer);
    if(length > subbuffers_[buffer].filled) {
        throw std::runtime_error("Pop length longer than filled.");
    }
    subbuffers_[buffer].filled -= length;
}

void RenderBuffer::clear(SubBuffer buffer) {
    check_subbuffer(buffer);
    subbuffers_[buffer].filled = 0;
}

void RenderBuffer::delete_subbuffer(SubBuffer buffer) {
    check_subbuffer(buffer);
    subbuffers_[buffer].filled = 0;
    recycle_.insert(buffer);
}
