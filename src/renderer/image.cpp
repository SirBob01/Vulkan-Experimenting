#include "image.h"

vk::UniqueImage create_image(vk::Device &logical,
                             uint32_t width,
                             uint32_t height,
                             uint32_t mip_levels,
                             vk::Format format,
                             vk::ImageTiling tiling, 
                             vk::Flags<vk::ImageUsageFlagBits> usage,
                             vk::SampleCountFlagBits samples) {
    vk::ImageCreateInfo image_info;
    image_info.imageType = vk::ImageType::e2D;
    image_info.format = format;

    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;

    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = samples;

    image_info.tiling = tiling;
    image_info.usage = usage;
    image_info.sharingMode = vk::SharingMode::eExclusive;

    return logical.createImageUnique(image_info);
}

vk::UniqueImageView create_view(vk::Device &logical,
                                vk::Image &image,
                                vk::Format format, 
                                vk::Flags<vk::ImageAspectFlagBits> aspect_mask, 
                                uint32_t mip_levels) {
    vk::ImageViewCreateInfo view_info;
    view_info.image = image;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = format;

    view_info.subresourceRange.aspectMask = aspect_mask;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    return logical.createImageViewUnique(view_info);
}
