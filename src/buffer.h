#ifndef RENDER_BUFFER_H_
#define RENDER_BUFFER_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION

#include <vulkan/vulkan.hpp>

#include <cstring>
#include <set>

#include "physical.h"
#include "util.h"

// An integer handle to a SubBuffer in a buffer
using SubBuffer = int;

// RenderBuffer represents an allocated block of memory in
// the device. It can be resized similar to realloc and
// subbuffers can be allocated from it.
class RenderBuffer {
    vk::Device logical_;
    PhysicalDevice &physical_;

    vk::UniqueBuffer handle_;
    vk::UniqueDeviceMemory memory_;

    vk::BufferUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;

    vk::CommandBuffer command_buffer_;
    vk::Queue transfer_queue_;

    size_t offset_alignment_;

    size_t length_;
    bool host_visible_;
    char *bind_;

    struct SubBufferData {
        size_t size;
        size_t offset;
        size_t filled;
    };

    std::vector<SubBufferData> subbuffers_;
    std::set<SubBuffer> recycle_;

    // Initialize the buffer handle
    void initialize_buffer();

    // Allocate memory in the GPU for the buffer
    void alloc_memory();

    // Copy data to another RenderBuffers using offsets
    void copy_to_offset(RenderBuffer &target, size_t length,
                        size_t src_offset, size_t dst_offset);

    // Resize the entire buffer
    // This is an expensive call, so allocate large upfront
    void resize(size_t size);

    // Re-suballocate a subbuffer
    // Adjusts internal offsets and shifts data
    void resuballoc(SubBuffer buffer, size_t size);

    // Ensure that a given subbuffer is allocated
    void check_subbuffer(SubBuffer buffer);

public:
    RenderBuffer(size_t length,
                 vk::Device &logical, 
                 PhysicalDevice &physical,
                 vk::BufferUsageFlags usage, 
                 vk::MemoryPropertyFlags properties,
                 vk::CommandBuffer &command_buffer, 
                 vk::Queue &transfer_queue);
    ~RenderBuffer();

    // Get the length of the buffer
    size_t get_size();

    // Get the handle to the Vulkan buffer
    vk::Buffer &get_handle();

    // Get the number of subbuffers
    int get_subbuffer_count();

    // Get the offset of a subbuffer
    size_t get_offset(SubBuffer buffer);

    // Get the fill of a subbuffer
    size_t get_subfill(SubBuffer buffer);

    // Get the size of a subbuffer
    size_t get_subsize(SubBuffer buffer);

    // Get the pointer to mapped data
    // Use this to read from GPU buffer
    char *get_mapped();

    // Suballocate at the end of the buffer and return the handle
    SubBuffer suballoc(size_t size);

    // Copy CPU data into a GPU subbuffer
    void copy(SubBuffer buffer, void *data, size_t length);
    
    // Copy data to another RenderBuffer
    void copy_buffer(RenderBuffer &target, size_t length,
                     SubBuffer src, SubBuffer dst);

    // Raw copy CPU data to the GPU buffer without considering subbuffers
    // Unsafe! Do not use in conjunction with subbuffer management
    void copy_raw(void *data, size_t length, size_t offset);

    // Remove a length of bytes from a subbuffer starting at an offset
    void remove(SubBuffer buffer, size_t offset, size_t length);

    // Treat the subbuffer as a stack and pop the last length of bytes
    void pop(SubBuffer buffer, size_t length);

    // Clear the contents of a subbuffer
    void clear(SubBuffer buffer);

    // Delete a subbuffer to be recycled on the next call to suballoc()
    void delete_subbuffer(SubBuffer buffer);
};

#endif