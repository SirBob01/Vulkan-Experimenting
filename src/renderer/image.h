#ifndef IMAGE_H_
#define IMAGE_H_

#include <vulkan/vulkan.hpp>

vk::UniqueImage create_image(vk::Device &logical,
                             uint32_t width,
                             uint32_t height,
                             uint32_t mip_levels,
                             vk::Format format,
                             vk::ImageTiling tiling, 
                             vk::Flags<vk::ImageUsageFlagBits> usage,
                             vk::SampleCountFlagBits samples);

vk::UniqueImageView create_view(vk::Device &logical,
                                vk::Image &image,
                                vk::Format format, 
                                vk::Flags<vk::ImageAspectFlagBits> aspect_mask, 
                                uint32_t mip_levels);

#endif