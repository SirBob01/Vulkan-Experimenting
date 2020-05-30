#define VULKAN_HPP_TYPESAFE_CONVERSION
#include <vulkan/vulkan.hpp>

#include <cstring>

// An integer handle to a SubBuffer in a buffer
using SubBuffer = unsigned;

// Wrapper class for Vulkan buffer objects
class RenderBuffer {
    vk::Device logical_;
    vk::UniqueBuffer handle_;
    vk::UniqueDeviceMemory memory_;

    vk::BufferUsageFlags usage_;
    vk::MemoryPropertyFlags properties_;
    vk::PhysicalDeviceMemoryProperties device_spec_;

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

    // Ensure that a given subbuffer is allocated
    void check_subbuffer(SubBuffer buffer);

public:
    RenderBuffer(vk::Device &logical, size_t length, 
                 vk::BufferUsageFlags usage, 
                 vk::MemoryPropertyFlags properties,
                 vk::PhysicalDeviceMemoryProperties device_spec);
    ~RenderBuffer();

    // Get the length of the buffer
    size_t get_length();

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

    // Suballocate at the end of the buffer and return the handle
    SubBuffer suballoc(size_t size);

    // Re-suballocate a subbuffer
    // Adjusts internal offsets and shifts data
    void resuballoc(SubBuffer buffer, size_t size);

    // Clear the contents of a subbuffer
    void clear(SubBuffer buffer);

    // Copy data into a subbuffer
    void copy(SubBuffer buffer, void *data, int length);

    // Raw copy data to the buffer without considering subbuffers
    void copy_raw(void *data, int length, int offset);
};