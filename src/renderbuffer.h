#ifndef RENDER_BUFFER_H_
#define RENDER_BUFFER_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION
#include <vulkan/vulkan.hpp>

#include <cstring>
#include <iostream>
#include "physical.h"

// An integer handle to a SubBuffer in a buffer
using SubBuffer = int;

// Wrapper class for Vulkan buffer objects
// User is responsible for updating command buffer listeners
class RenderBuffer {
    vk::Device logical_;
    PhysicalDevice &physical_;

    vk::UniqueBuffer handle_;
    vk::UniqueDeviceMemory memory_;

    vk::BufferUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;

    vk::CommandBuffer copier_;
    vk::Queue transfer_queue_;

    size_t length_;
    bool host_visible_;
    char *bind_;

    struct SubBufferData {
        size_t size;
        size_t offset;
        size_t filled;
    };

    std::vector<SubBufferData> subbuffers_;

    // Initialize the buffer handle
    void initialize_buffer();

    // Allocate memory in the GPU for the buffer
    void alloc_memory();

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
                 vk::CommandBuffer &copier, 
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

    // Clear the contents of a subbuffer
    void clear(SubBuffer buffer);

    // Copy CPU data into a GPU subbuffer
    void copy(SubBuffer buffer, void *data, int length);
    
    // Copy data to another RenderBuffer
    void copy_to(RenderBuffer &target, int length,
                 SubBuffer src, SubBuffer dst);

    // Copy data to another RenderBuffers using raw offsets
    void copy_to_raw(RenderBuffer &target, int length,
                     int src_offset, int dst_offset);

    // Raw copy CPU data to the GPU buffer without considering subbuffers
    void copy_raw(void *data, int length, int offset);
};

#endif