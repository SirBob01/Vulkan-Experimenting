#include <vulkan/vulkan.hpp>

#include <cstring>

using SubBuffer = unsigned;

class RenderBuffer {
    vk::Device logical_;
    vk::UniqueBuffer handle_;
    vk::UniqueDeviceMemory memory_;

    size_t length_;
    bool host_visible_;
    void *bind_;

    std::vector<int> offsets_;

    // Initialize the buffer handle
    void initialize_buffer(vk::BufferUsageFlags usage);

    // Allocate memory in the GPU for the buffer
    void alloc_memory(vk::MemoryPropertyFlags properties, 
                      vk::PhysicalDeviceMemoryProperties available);

public:
    RenderBuffer(vk::UniqueDevice &logical, size_t length, 
                 vk::BufferUsageFlags usage, 
                 vk::MemoryPropertyFlags properties,
                 vk::PhysicalDeviceMemoryProperties device_spec);
    ~RenderBuffer();

    // Get the length of the buffer
    size_t get_length();

    // Get the handle to the Vulkan buffer
    vk::Buffer &get_handle();

    // Copy the data to the buffer
    void copy(void *usr_data, int length);

// TODO: Implement these
    // Suballocate a new buffer and return the index to its offset
    SubBuffer suballocate(size_t size);

    // Get the offset of the subbuffer
    size_t get_offset(SubBuffer subbuffer);
};