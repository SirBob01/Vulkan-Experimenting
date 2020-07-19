#ifndef COMMANDS_H_
#define COMMANDS_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION

#include <vulkan/vulkan.hpp>

// An integer handle to a command buffer held by RenderCommands
using CommandBuffer = unsigned;

// Wrapper class for command buffer management
class RenderCommands {
    vk::Device logical_;
    vk::UniqueCommandPool pool_;
    std::vector<vk::CommandBuffer> buffers_;

public:
    RenderCommands(vk::Device &logical, uint32_t queue_family, 
                   vk::CommandPoolCreateFlags flags);

    // Allocate a new command buffer
    CommandBuffer allocate_buffer(vk::CommandBufferLevel level);

    // Record to a command buffer by passing a lambda
    // This lambda should comply with Vulkan specs
    // on the life-cycle of a command buffer
    template <class Func>
    void record(CommandBuffer buffer, Func func,
                vk::CommandBufferUsageFlags flags={}) {
        auto command_buffer = buffers_[buffer];
        vk::CommandBufferBeginInfo begin_info(
            flags
        );
        command_buffer.begin(begin_info);
        func(command_buffer);
        command_buffer.end();
    }

    // Record for all command buffers
    template <class Func>
    void record_all(Func func, vk::CommandBufferUsageFlags flags={}) {
        for(int i = 0; i < buffers_.size(); i++) {
            record(i, func, flags);
        }
    }
};

#endif