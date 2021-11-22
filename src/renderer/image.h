#ifndef IMAGE_H_
#define IMAGE_H_

#include <vulkan/vulkan.hpp>

#include <unordered_map>
#include <set>
#include <memory>

#include "physical.h"
#include "util.h"

// Create a unique image
vk::UniqueImage create_image(vk::Device &logical,
                             uint32_t width,
                             uint32_t height,
                             uint32_t mip_levels,
                             vk::Format format,
                             vk::ImageTiling tiling, 
                             vk::Flags<vk::ImageUsageFlagBits> usage,
                             vk::SampleCountFlagBits samples);

// Create a unique image view
vk::UniqueImageView create_view(vk::Device &logical,
                                vk::Image &image,
                                vk::Format format, 
                                vk::Flags<vk::ImageAspectFlagBits> aspect_mask, 
                                uint32_t mip_levels);

// Describes image memory requirements
struct MemoryMeta {
    int memory_type;
    size_t alignment;

    bool operator==(const MemoryMeta &other) const;
};

// Custom hash function for vertices
template <>
struct std::hash<MemoryMeta> {
    size_t operator()(MemoryMeta const &memory_meta) const;
};

// A handle to an image memory binding
struct ImageMemoryHandle {
    MemoryMeta memory_meta;
    int pool;
    int index;
};

// A memory pool for images
class ImagePool {
    struct Binding {
        size_t size;
        size_t offset;
    };

    vk::Device logical_;
    size_t capacity_;
    size_t alignment_;

    vk::UniqueDeviceMemory memory_;
    std::vector<Binding> bindings_;
    std::set<int> recycle_;

public:
    ImagePool(vk::Device &logical, 
              MemoryMeta &memory_meta);

    // Suballocate an image to the pool
    // This will attempt to defragment memory by recycling old bindings
    int suballoc(vk::Image &image);

    // Remove an image from the pool
    void remove(int index);
};

// Handles memory for images (e.g., textures, depth buffer, etc.)
class ImageMemoryAllocator {
    vk::Device logical_;
    PhysicalDevice &physical_;

    std::unordered_map<MemoryMeta, 
                       std::vector<std::unique_ptr<ImagePool>>> memory_;

    // Get the memory requirements for an image
    MemoryMeta get_memory_meta(vk::Image &image);

public:
    ImageMemoryAllocator(vk::Device &logical, 
                         PhysicalDevice &physical);

    // Allocate memory for an image and return the handle
    // If an image cannot be added any existing pool, create a new one
    ImageMemoryHandle allocate_memory(vk::Image &image);

    // Remove a memory allocation for an image
    void remove_image(ImageMemoryHandle handle);

    // Reset all pools
    // Assumes that all images bound to the pools have been destroyed
    void reset();
};

#endif