#ifndef RENDERER_H_
#define RENDERER_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION
#define STB_IMAGE_IMPLEMENTATION
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <vulkan/vulkan.hpp>
#include <SDL2/SDL_vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include "assets/stb_image.h"

#include <chrono>

#include <vector>

#include <exception>
#include <iostream>
#include <fstream>
#include <memory>

#include "pipeline.h"
#include "texture.h"
#include "buffer.h"
#include "physical.h"
#include "model.h"
#include "vertex.h"
#include "debug.h"
#include "util.h"

#ifndef NDEBUG
    #define DEBUG true
#else
    #define DEBUG false
#endif

using ShaderBytes = std::vector<char>; // Vulkan shader bytecode for parsing

struct PushConstantObject {
    int texture;
};

struct UniformBufferObject {
    alignas(16) glm::mat4 transform;
};

struct MeshData {
    SubBuffer vertexes;
    SubBuffer indexes;
    Texture texture = 0;
};

// TODO: Implement better command buffer management
// Idea: Create classes of command pools (static/dynamic)
class Renderer {
    SDL_Window *window_;

    // Required extensions and validation layers
    std::vector<const char *> extensions_;
    std::vector<const char *> validation_layers_;

    // Debug messenger extension
    std::unique_ptr<RenderDebug> debugger_;

    // Instance of the Vulkan renderer
    vk::UniqueInstance instance_;

    // Vulkan surface
    vk::UniqueSurfaceKHR surface_;

    // Devices
    std::unique_ptr<PhysicalDevice> physical_;
    vk::UniqueDevice logical_;

    // Swapchain and its images
    vk::UniqueSwapchainKHR swapchain_;
    std::vector<vk::Image> images_;
    std::vector<vk::UniqueImageView> views_;
    
    // Depth buffer
    vk::UniqueImage depth_image_;
    vk::UniqueDeviceMemory depth_image_memory_;
    vk::UniqueImageView depth_view_;

    // Image meta data
    vk::Extent2D image_extent_;
    vk::Format image_format_;

    // Descriptor set
    vk::UniqueDescriptorSetLayout descriptor_layout_;
    vk::UniqueDescriptorPool descriptor_pool_;
    std::vector<vk::UniqueDescriptorSet> descriptor_sets_;

    // Graphics pipeline
    vk::UniqueRenderPass render_pass_;
    std::unique_ptr<Pipeline> pipeline_;

    // Framebuffers
    std::vector<vk::UniqueFramebuffer> framebuffers_;

    // Command pool (memory buffer for commands)
    vk::UniqueCommandPool graphics_pool_;
    vk::UniqueCommandPool transfer_pool_;

    // Command buffer (recording commands)
    std::vector<vk::UniqueCommandBuffer> graphics_commands_;
    vk::UniqueCommandBuffer transfer_commands_; // For copying

    // Command Queues (submitting commands)
    AvailableQueues queues_;
    vk::Queue graphics_queue_;
    vk::Queue present_queue_;
    vk::Queue transfer_queue_;

    // Data buffers
    // Object buffer is a one-size-fits-all for vertex and index data
    std::unique_ptr<RenderBuffer> staging_buffer_;
    std::unique_ptr<RenderBuffer> object_buffer_;
    std::vector<MeshData> mesh_data_;
    size_t buffer_size_;

    // Uniform buffers
    std::unique_ptr<RenderBuffer> uniform_buffer_;

    // Texture handling
    std::vector<std::unique_ptr<TextureData>> textures_;
    vk::UniqueSampler texture_sampler_;

    // Semaphores
    // Ensures correct ordering between graphic and present commands
    std::vector<vk::UniqueSemaphore> image_available_signal_;
    std::vector<vk::UniqueSemaphore> render_finished_signal_;

    // Fences
    // Synchronizes between GPU and CPU processes
    std::vector<vk::UniqueFence> fences_;
    std::vector<vk::Fence> active_fences_;

    // Frame processing indices
    int max_frames_processing_;
    int current_frame_;

    // Clear value for viewport refresh
    vk::ClearValue clear_value_;
    vk::ClearValue depth_clear_value_;

    // Mip levels for all textures
    uint32_t mip_levels_;

    // Sampling count for MSAA
    vk::SampleCountFlagBits msaa_samples_;
    vk::UniqueImage color_image_;
    vk::UniqueDeviceMemory color_image_memory_;
    vk::UniqueImageView color_image_view_;

    bool vsync_ = false;

    // Get all required Vulkan extensions from SDL
    void get_extensions() {
        unsigned int count;
        SDL_Vulkan_GetInstanceExtensions(window_, &count, nullptr);

        extensions_.resize(count);
        SDL_Vulkan_GetInstanceExtensions(window_, &count, &extensions_[0]);

        if constexpr(DEBUG) {
            validation_layers_.push_back("VK_LAYER_KHRONOS_validation");
            extensions_.push_back("VK_EXT_debug_utils");
            std::cerr << "Vulkan Extensions:\n";
            for(auto &extension : extensions_) {
                std::cerr << "* " << extension << "\n";
            }
            std::cerr << "\n";
        }
    }

    // Check if the system supports validation layers
    bool is_supporting_layers() {
        auto layer_properties = vk::enumerateInstanceLayerProperties();
        for(auto &requested : validation_layers_) {
            for(auto &available : layer_properties) {
                if(!strcmp(requested, available.layerName)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Create the Vulkan instance
    void create_instance() {
        if(!is_supporting_layers() && DEBUG) {
            throw std::runtime_error("Requested Vulkan layers unavailable.");
        }

        // Setup the application and Vulkan instance
        vk::ApplicationInfo app_info;
        app_info.pApplicationName = SDL_GetWindowTitle(window_);
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "Dynamo Engine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_MAKE_VERSION(1, 2, 0);

        vk::InstanceCreateInfo instance_info;
        instance_info.pApplicationInfo = &app_info;

        // Load validation layers
        instance_info.enabledLayerCount = validation_layers_.size();
        instance_info.ppEnabledLayerNames = &validation_layers_[0];

        // Load extensions
        instance_info.enabledExtensionCount = extensions_.size();
        instance_info.ppEnabledExtensionNames = &extensions_[0];

        // Include more validation layers on debug mode
        std::vector<vk::ValidationFeatureEnableEXT> layer_extensions = {
            vk::ValidationFeatureEnableEXT::eBestPractices,
            vk::ValidationFeatureEnableEXT::eDebugPrintf
        };
        vk::ValidationFeaturesEXT features_info(layer_extensions);
        if constexpr(DEBUG) {
            instance_info.pNext = &features_info;
        }

        // Create the instance and, if necessary, the debugger
        instance_ = vk::createInstanceUnique(instance_info);
        if constexpr(DEBUG) {
            debugger_ = std::make_unique<RenderDebug>(
                instance_
            );
        }
    }

    // Attach the SDL_Window to a Vulkan surface
    void create_surface() {
        VkSurfaceKHR temp_surface;
        bool result = SDL_Vulkan_CreateSurface(
            window_, instance_.get(), &temp_surface
        );
        surface_ = vk::UniqueSurfaceKHR(temp_surface, instance_.get());
        if(!result) {
            throw std::runtime_error("Unable to create Vulkan surface!");
        }
    }

    // Choose the best available physical device
    void create_physical_device() {
        auto devices = instance_->enumeratePhysicalDevices();

        PhysicalDevice best(devices[0], surface_.get());
        for(auto &physical : devices) {
            PhysicalDevice card(physical, surface_.get());
            if(card.get_score() > best.get_score()) {
                best = card;
            }
        }
        
        if constexpr(DEBUG) {
            std::cerr << "Physical Devices:\n";
            for(auto &physical : devices) {
                auto properties = physical.getProperties();
                std::cerr << "* " << properties.deviceName << "\n";
            }
            std::cerr << best.get_name() << " selected.\n\n";
        }

        if(!best.get_score()) {
            throw std::runtime_error("Vulkan could not find suitable GPU.");
        }

        physical_ = std::make_unique<PhysicalDevice>(best);
        msaa_samples_ = get_sample_count();
    }

    // Create the logical device from the chosen physical device
    // Generate the required queues for this device
    void create_logical_device() {
        auto &physical_handle = physical_->get_handle();
        queues_ = physical_->get_available_queues();

        // Get all unique queue families
        auto cmp = [](QueueFamily a, QueueFamily b) { 
            return a.index != b.index;
        };
        std::set<QueueFamily, decltype(cmp)> unique_families(cmp);
        unique_families.insert(queues_.graphics);
        unique_families.insert(queues_.present);
        unique_families.insert(queues_.transfer);

        // Allocate queues
        std::vector<vk::DeviceQueueCreateInfo> queue_infos;
        for(auto &family : unique_families) {
            // Priorities influence scheduling command buffer execution
            std::vector<float> priorities(family.count, 0.0f);
            
            vk::DeviceQueueCreateInfo queue_info;
            queue_info.queueFamilyIndex = family.index;
            queue_info.queueCount = family.count;
            queue_info.pQueuePriorities = &priorities[0];
            
            queue_infos.push_back(queue_info);
        }

        // Enable certain features of the physical device
        vk::PhysicalDeviceFeatures device_features;
        device_features.samplerAnisotropy = true;
        device_features.sampleRateShading = true;
        device_features.fillModeNonSolid = true;
        device_features.wideLines = true;

        vk::PhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features;
        descriptor_indexing_features.descriptorBindingPartiallyBound = true;
        descriptor_indexing_features.runtimeDescriptorArray = true;
        descriptor_indexing_features.descriptorBindingVariableDescriptorCount = true;

        // Create the logical device
        auto &device_extensions = physical_->get_extensions();
        
        vk::DeviceCreateInfo device_info;
        device_info.queueCreateInfoCount = queue_infos.size();
        device_info.pQueueCreateInfos = &queue_infos[0];
        device_info.enabledExtensionCount = device_extensions.size();
        device_info.ppEnabledExtensionNames = &device_extensions[0];
        device_info.pEnabledFeatures = &device_features;
        device_info.pNext = &descriptor_indexing_features;

        logical_ = physical_handle.createDeviceUnique(device_info);
        
        // Grab device queue handles
        graphics_queue_ = logical_->getQueue(
            queues_.graphics.index, 0
        );
        present_queue_  = logical_->getQueue(
            queues_.present.index, 0
        );
        transfer_queue_  = logical_->getQueue(
            queues_.transfer.index, 0
        );
    }

    // Get the dimensions of the swapchain (viewport)
    vk::Extent2D get_swapchain_extent(const vk::SurfaceCapabilitiesKHR &supported) {
        int width, height;
        SDL_Vulkan_GetDrawableSize(window_, &width, &height);

        vk::Extent2D extent;
        extent.width = clamp(
            static_cast<uint32_t>(width), 
            supported.minImageExtent.width, 
            supported.maxImageExtent.width
        );
        extent.height = clamp(
            static_cast<uint32_t>(height), 
            supported.minImageExtent.height, 
            supported.maxImageExtent.height
        );
        return extent;
    }

    // Choose the format with the appropriate color space
    vk::SurfaceFormatKHR get_swapchain_format(const std::vector<vk::SurfaceFormatKHR> &supported) {
        for(auto &format : supported) {
            if(format.format == vk::Format::eB8G8R8A8Srgb && 
               format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }
        return supported[0];
    }

    // Choose how the swapchain presents images to surface
    // Allows the renderer to perform vsync
    vk::PresentModeKHR get_swapchain_presentation(const std::vector<vk::PresentModeKHR> &supported) {
        for(auto &mode : supported) {
            if(!vsync_ && mode == vk::PresentModeKHR::eImmediate) {
                return mode;
            }
            else if(mode == vk::PresentModeKHR::eMailbox) {
                return mode;
            }
        }
        return vk::PresentModeKHR::eFifo;
    }

    // Create the swapchain
    // Swapchain is the collection of images to be worked with
    void create_swapchain() {
        auto &supported = physical_->get_swapchain_support();

        auto extent = get_swapchain_extent(supported.capabilities);
        auto format = get_swapchain_format(supported.formats);
        auto presentation = get_swapchain_presentation(supported.presents);

        // Determines the number of images to be rendered to
        // This allows multiple buffering
        uint32_t image_count = supported.capabilities.minImageCount + 1;
        if(supported.capabilities.maxImageCount) {
            image_count = std::min(
                image_count,
                supported.capabilities.maxImageCount
            );
        }

        // Define the swapchain
        vk::SwapchainCreateInfoKHR swapchain_info;
        swapchain_info.surface = surface_.get();
        swapchain_info.minImageCount = image_count;
        swapchain_info.preTransform = supported.capabilities.currentTransform;
        swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchain_info.presentMode = presentation;
        swapchain_info.clipped = true;

        // Define shared properties for each swapchain image
        swapchain_info.imageFormat = format.format;
        swapchain_info.imageColorSpace = format.colorSpace;
        swapchain_info.imageExtent = extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;

        // Allow multiple queues to access buffers/images concurrently
        std::vector<uint32_t> index_arr = {
            queues_.present.index
        };
        if(queues_.present.index != queues_.graphics.index) {
            index_arr.push_back(queues_.graphics.index);
        }
        if(queues_.present.index != queues_.transfer.index) {
            index_arr.push_back(queues_.transfer.index);
        }
        if(index_arr.size() > 1) {
            swapchain_info.imageSharingMode = vk::SharingMode::eConcurrent;
            swapchain_info.queueFamilyIndexCount = index_arr.size();
            swapchain_info.pQueueFamilyIndices = &index_arr[0];
        }

        // Create the swapchain and its images
        swapchain_ = logical_->createSwapchainKHRUnique(swapchain_info);
        images_ = logical_->getSwapchainImagesKHR(swapchain_.get());

        // Store image meta data for rendering pipeline
        image_extent_ = extent;
        image_format_ = format.format;
    }

    // Create views to each swapchain image
    // Views are simply references to a specific part of an image
    void create_views() {
        vk::ComponentMapping color_components(
            vk::ComponentSwizzle::eR,
            vk::ComponentSwizzle::eG,
            vk::ComponentSwizzle::eB,
            vk::ComponentSwizzle::eA
        );

        vk::ImageSubresourceRange subresource_range;
        subresource_range.aspectMask = vk::ImageAspectFlagBits::eColor,
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = 1;
        subresource_range.baseArrayLayer = 0;
        subresource_range.layerCount = 1;

        // Create views for each image
        for(auto &image : images_) {
            vk::ImageViewCreateInfo view_info;
            view_info.image = image;
            view_info.viewType = vk::ImageViewType::e2D;
            view_info.format = image_format_;
            
            view_info.components = color_components;
            view_info.subresourceRange = subresource_range;

            views_.push_back(
                std::move(
                    logical_->createImageViewUnique(view_info)
                )
            );
        }
    }

    // Create the descriptor set layout
    // Describes the data passed to a shader stage
    void create_descriptor_layout() {
        // UBO layout binding
        vk::DescriptorSetLayoutBinding ubo_layout_binding;
        ubo_layout_binding.binding = 0;
        ubo_layout_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        ubo_layout_binding.descriptorCount = 1;
        ubo_layout_binding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Image layout sampler binding (supports variable count textures)
        uint32_t max_samplers = physical_->get_limits().maxPerStageDescriptorSamplers;
        vk::DescriptorSetLayoutBinding sampler_layout_binding;
        sampler_layout_binding.binding = 1;
        sampler_layout_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        sampler_layout_binding.descriptorCount = max_samplers;
        sampler_layout_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            ubo_layout_binding, 
            sampler_layout_binding
        };

        // Set binding flags
        std::vector<vk::DescriptorBindingFlags> flags = {
            vk::DescriptorBindingFlagBitsEXT::ePartiallyBound,
            vk::DescriptorBindingFlagBitsEXT::eVariableDescriptorCount
        };
        vk::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info;
        binding_flags_info.bindingCount = flags.size();
        binding_flags_info.pBindingFlags = &flags[0];

        // Create the descriptor set layout
        vk::DescriptorSetLayoutCreateInfo descriptor_layout_info;
        descriptor_layout_info.bindingCount = bindings.size();
        descriptor_layout_info.pBindings = &bindings[0];
        descriptor_layout_info.pNext = &binding_flags_info;
        
        descriptor_layout_ = logical_->createDescriptorSetLayoutUnique(
            descriptor_layout_info
        );
    }

    // TODO: Move this to its own class
    // Load the bytecode of shader modules
    ShaderBytes load_shader(std::string filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if(!file.is_open()) {
            throw std::runtime_error("Failed to load shader: " + filename);
        }

        size_t size = file.tellg();
        ShaderBytes bytes(size);
        
        file.seekg(0);
        file.read(&bytes[0], size);
        file.close();

        return bytes;
    }

    // Create a shader module for the graphics pipeline from bytecode
    vk::UniqueShaderModule create_shader(ShaderBytes code) {
        vk::ShaderModuleCreateInfo shader_info;
        shader_info.codeSize = code.size();
        shader_info.pCode = reinterpret_cast<uint32_t *>(&code[0]);

        return logical_->createShaderModuleUnique(
            shader_info
        );
    }

    // Initialize the render pass
    // Describes the scope of a render operation (color, depth, stencil)
    // Lists attachments, dependencies, and subpasses
    // Driver knows what to expect for render operation
    // TODO: Make this its own class to allow custom render passes and attachments
    void create_render_pass() {
        // Color buffer for a single swapchain image
        vk::AttachmentDescription color_attachment;
        color_attachment.format = image_format_;
        color_attachment.samples = msaa_samples_;

        // Overwrite old buffer data
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        
        color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

        // Transition to another color attachment layout for multisampling
        color_attachment.initialLayout = vk::ImageLayout::eUndefined;
        color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference color_ref;
        color_ref.attachment = 0;
        color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth buffer
        vk::AttachmentDescription depth_attachment;
        depth_attachment.format = get_depth_format();
        depth_attachment.samples = msaa_samples_;

        // Clear old buffer data
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        
        depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

        depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
        depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depth_ref;
        depth_ref.attachment = 1;
        depth_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        // Color resolve buffer for multisampling
        vk::AttachmentDescription color_resolve_attachment;
        color_resolve_attachment.format = image_format_;
        color_resolve_attachment.samples = vk::SampleCountFlagBits::e1;

        // Overwrite old buffer data
        color_resolve_attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
        color_resolve_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        
        color_resolve_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        color_resolve_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

        // Transition layout to something presentable to the screen
        color_resolve_attachment.initialLayout = vk::ImageLayout::eUndefined;
        color_resolve_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference color_resolve_ref;
        color_resolve_ref.attachment = 2;
        color_resolve_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Subpasses
        vk::SubpassDescription initial_subpass;
        initial_subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        initial_subpass.colorAttachmentCount = 1;
        initial_subpass.pColorAttachments = &color_ref;
        initial_subpass.pDepthStencilAttachment = &depth_ref;
        initial_subpass.pResolveAttachments = &color_resolve_ref;

        // Define subpass dependencies
        vk::SubpassDependency subpass_dependency;
        subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpass_dependency.dstSubpass = 0;
        subpass_dependency.srcStageMask = 
            vk::PipelineStageFlagBits::eColorAttachmentOutput | 
            vk::PipelineStageFlagBits::eEarlyFragmentTests;
        subpass_dependency.dstStageMask = 
            vk::PipelineStageFlagBits::eColorAttachmentOutput | 
            vk::PipelineStageFlagBits::eEarlyFragmentTests;
        subpass_dependency.srcAccessMask = vk::AccessFlagBits::eNoneKHR;
        subpass_dependency.dstAccessMask = 
            vk::AccessFlagBits::eColorAttachmentWrite | 
            vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        
        // Create the render pass
        std::vector<vk::SubpassDescription> subpasses = {
            initial_subpass
        };
        std::vector<vk::AttachmentDescription> attachments = {
            color_attachment, 
            depth_attachment, 
            color_resolve_attachment
        };
        std::vector<vk::SubpassDependency> dependencies = {
            subpass_dependency
        };
        vk::RenderPassCreateInfo render_pass_info;
        render_pass_info.attachmentCount = attachments.size();
        render_pass_info.pAttachments = &attachments[0];
        render_pass_info.subpassCount = subpasses.size();
        render_pass_info.pSubpasses = &subpasses[0];
        render_pass_info.dependencyCount = dependencies.size();
        render_pass_info.pDependencies = &dependencies[0];
        
        render_pass_ = logical_->createRenderPassUnique(render_pass_info);
    }

    // Initialize all stages of the graphics pipeline
    // This determines what and how things are drawn
    // This is the heart of the Renderer, fixed at runtime
    // We can have multiple pipelines for different types of rendering
    void create_graphics_pipeline() {
        pipeline_ = std::make_unique<Pipeline>(
            logical_.get(),
            image_extent_,
            descriptor_layout_.get(),
            render_pass_.get(),
            "base.vert.spv",
            "base.frag.spv",
            vk::PrimitiveTopology::eTriangleList,
            vk::PolygonMode::eFill,
            msaa_samples_,
            sizeof(PushConstantObject)
        );
    }

    // Create the framebuffers for each swapchain image
    // Framebuffers hold the memory attachments used by render pass
    // Ex. color image buffer and depth buffer
    void create_framebuffers() {
        for(auto &view : views_) {
            std::vector<vk::ImageView> views = {
                color_image_view_.get(),
                depth_view_.get(),
                view.get()
            };
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.renderPass = render_pass_.get();
            framebuffer_info.attachmentCount = views.size();
            framebuffer_info.pAttachments = &views[0];
            framebuffer_info.width = image_extent_.width;
            framebuffer_info.height = image_extent_.height;
            framebuffer_info.layers = 1;

            framebuffers_.push_back(std::move(
                    logical_->createFramebufferUnique(
                        framebuffer_info
                    )
                )
            );
        }
    }

    // Create the command pools that manage command buffers
    // for each device queue family
    void create_command_pool() {
        // Command pool for the graphics queue
        vk::CommandPoolCreateInfo graphics_pool_info;
        graphics_pool_info.queueFamilyIndex = queues_.graphics.index;
        graphics_pool_ = logical_->createCommandPoolUnique(graphics_pool_info);

        // Command pool for the transfer queue
        vk::CommandPoolCreateInfo transfer_pool_info;
        transfer_pool_info.queueFamilyIndex = queues_.transfer.index;
        transfer_pool_ = logical_->createCommandPoolUnique(transfer_pool_info);
    }

    // Allocate buffers for submitting commands
    void create_command_buffers() {
        vk::CommandBufferAllocateInfo graphics_cmd_alloc_info;
        graphics_cmd_alloc_info.commandPool = graphics_pool_.get();
        graphics_cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
        graphics_cmd_alloc_info.commandBufferCount = framebuffers_.size();
        graphics_commands_ = logical_->allocateCommandBuffersUnique(
            graphics_cmd_alloc_info
        );

        // Create a command buffer for copying between data buffers
        // This is not attached to any pipeline stage or semaphores
        vk::CommandBufferAllocateInfo transfer_cmd_alloc_info;
        transfer_cmd_alloc_info.commandPool = transfer_pool_.get();
        transfer_cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
        transfer_cmd_alloc_info.commandBufferCount = 1;
        transfer_commands_ = std::move(
            logical_->allocateCommandBuffersUnique(transfer_cmd_alloc_info)[0]
        );

        // Create the staging buffer for CPU to GPU copies
        staging_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, 
            logical_.get(), 
            *physical_, 
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | 
            vk::MemoryPropertyFlagBits::eHostCoherent |
            vk::MemoryPropertyFlagBits::eHostCached,
            transfer_commands_.get(),
            transfer_pool_.get(), 
            transfer_queue_
        );
        staging_buffer_->suballoc(buffer_size_);
    }

    // Create a sampler for loaded textures
    void create_texture_sampler() {
        vk::SamplerCreateInfo sampler_info;
        sampler_info.magFilter = vk::Filter::eLinear;
        sampler_info.minFilter = vk::Filter::eLinear;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;

        sampler_info.mipLodBias = 0.0f;
        
        sampler_info.anisotropyEnable = true;
        sampler_info.maxAnisotropy = physical_->get_limits().maxSamplerAnisotropy;

        sampler_info.compareEnable = false;
        sampler_info.compareOp = vk::CompareOp::eAlways;

        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = static_cast<float>(mip_levels_);
        
        sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
        sampler_info.unnormalizedCoordinates = false;

        texture_sampler_ = logical_->createSamplerUnique(sampler_info);
    }

    vk::Format get_depth_format() {
        std::vector<vk::Format> query = {
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint
        };
        vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
        vk::FormatFeatureFlags feature_flags = vk::FormatFeatureFlagBits::eDepthStencilAttachment;

        // Get an available format of the image
        vk::Format depth_image_format;
        for(vk::Format format : query) {
            auto props = physical_->get_format_properties(format);
            if(tiling == vk::ImageTiling::eLinear && 
               (props.linearTilingFeatures & feature_flags) == feature_flags) {
                return format;
            }
            else if(tiling == vk::ImageTiling::eOptimal && 
                    (props.optimalTilingFeatures & feature_flags) == feature_flags) {
                return format;
            }
        }

        throw std::runtime_error("Could not find a suitable format for the depth buffer.");
    }

    // Create the depth image
    void create_depth_image() {
        auto supported = physical_->get_swapchain_support();
        auto extent2D = get_swapchain_extent(supported.capabilities);

        vk::ImageCreateInfo image_info;
        image_info.imageType = vk::ImageType::e2D;
        image_info.format = get_depth_format();

        image_info.extent.width = extent2D.width;
        image_info.extent.height = extent2D.height;
        image_info.extent.depth = 1;
        
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = msaa_samples_;
        image_info.tiling = vk::ImageTiling::eOptimal;
        image_info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        image_info.sharingMode = vk::SharingMode::eExclusive;

        depth_image_ = logical_->createImageUnique(image_info);

        auto requirements = logical_->getImageMemoryRequirements(depth_image_.get());
        auto device_spec = physical_->get_memory();
        int memory_type = -1;
        for(uint32_t i = 0; i < device_spec.memoryTypeCount; i++) {
            if((requirements.memoryTypeBits & (1 << i)) && 
            (device_spec.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                memory_type = i;
                break;
            }
        }
        if(memory_type < 0) {
            throw std::runtime_error("Vulkan failed to allocate memory for a depth buffer.");
        }

        // Allocate memory for the depth buffer
        vk::MemoryAllocateInfo mem_alloc_info;
        mem_alloc_info.allocationSize = requirements.size;
        mem_alloc_info.memoryTypeIndex = memory_type;
        
        depth_image_memory_ = logical_->allocateMemoryUnique(
            mem_alloc_info
        );
        
        // Bind depth buffer to memory
        logical_->bindImageMemory(depth_image_.get(), depth_image_memory_.get(), 0);
    }

    // Create the depth image view
    void create_depth_view() {
        vk::ImageViewCreateInfo view_info;
        view_info.image = depth_image_.get();
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = get_depth_format();
        
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        depth_view_ = logical_->createImageViewUnique(view_info);
    }

    vk::SampleCountFlagBits get_sample_count() {
        auto &limits = physical_->get_limits();
        vk::SampleCountFlags counts = limits.framebufferColorSampleCounts;
        
        // Get the maximum available sample count for improved visuals
        if (counts & vk::SampleCountFlagBits::e64) return vk::SampleCountFlagBits::e64;
        if (counts & vk::SampleCountFlagBits::e32) return vk::SampleCountFlagBits::e32;
        if (counts & vk::SampleCountFlagBits::e16) return vk::SampleCountFlagBits::e16;
        if (counts & vk::SampleCountFlagBits::e8) return vk::SampleCountFlagBits::e8;
        if (counts & vk::SampleCountFlagBits::e4) return vk::SampleCountFlagBits::e4;
        if (counts & vk::SampleCountFlagBits::e2) return vk::SampleCountFlagBits::e2;
        return vk::SampleCountFlagBits::e1;
    }

    // Create the multisampling color buffer image
    void create_color_image() {
        auto supported = physical_->get_swapchain_support();
        auto extent2D = get_swapchain_extent(supported.capabilities);

        vk::ImageCreateInfo image_info;
        image_info.imageType = vk::ImageType::e2D;
        image_info.format = image_format_;

        image_info.extent.width = extent2D.width;
        image_info.extent.height = extent2D.height;
        image_info.extent.depth = 1;
        
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = msaa_samples_;
        image_info.tiling = vk::ImageTiling::eOptimal;
        image_info.usage = vk::ImageUsageFlagBits::eColorAttachment;
        image_info.sharingMode = vk::SharingMode::eExclusive;

        color_image_ = logical_->createImageUnique(image_info);

        auto requirements = logical_->getImageMemoryRequirements(color_image_.get());
        auto device_spec = physical_->get_memory();
        int memory_type = -1;
        for(uint32_t i = 0; i < device_spec.memoryTypeCount; i++) {
            if((requirements.memoryTypeBits & (1 << i)) && 
            (device_spec.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                memory_type = i;
                break;
            }
        }
        if(memory_type < 0) {
            throw std::runtime_error("Vulkan failed to allocate memory for a depth buffer.");
        }

        // Allocate memory for the depth buffer
        vk::MemoryAllocateInfo mem_alloc_info;
        mem_alloc_info.allocationSize = requirements.size;
        mem_alloc_info.memoryTypeIndex = memory_type;
        
        color_image_memory_ = logical_->allocateMemoryUnique(
            mem_alloc_info
        );
        
        // Bind depth buffer to memory
        logical_->bindImageMemory(color_image_.get(), color_image_memory_.get(), 0);
    }

    // Create the depth image view
    void create_color_view() {
        vk::ImageViewCreateInfo view_info;
        view_info.image = color_image_.get();
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = image_format_;
        
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        color_image_view_ = logical_->createImageViewUnique(view_info);
    }

    // Create the object buffer
    void create_object_buffer() {
        auto usage = vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eVertexBuffer;
        object_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, 
            logical_.get(), 
            *physical_,
            usage, 
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            transfer_commands_.get(), 
            transfer_pool_.get(), 
            transfer_queue_
        );
    }

    // Create a uniform buffer per swapchain image
    // This is where we store global data that is passed to all shaders
    // E.g., camera matrix, mouse pointer location, screen size, etc.
    void create_uniform_buffer() {
        uniform_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, 
            logical_.get(), 
            *physical_, 
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent |
            vk::MemoryPropertyFlagBits::eHostCached,
            transfer_commands_.get(), 
            transfer_pool_.get(), 
            transfer_queue_
        );

        // Ensure buffer offsets fit alignment requirements
        size_t size = round_up(
            sizeof(UniformBufferObject),
            physical_->get_limits().minUniformBufferOffsetAlignment
        );
        for(int i = 0; i < images_.size(); i++) {
            uniform_buffer_->suballoc(size);
        }
    }

    // Create the pool that manages all descriptor sets
    void create_descriptor_pool() {
        // Size of the UBO descriptors
        vk::DescriptorPoolSize ubo_pool_size;
        ubo_pool_size.type = vk::DescriptorType::eUniformBuffer;
        ubo_pool_size.descriptorCount = images_.size();

        // Size of the sampler descriptors
        uint32_t max_samplers = physical_->get_limits().maxPerStageDescriptorSamplers;
        vk::DescriptorPoolSize sampler_pool_size;
        sampler_pool_size.type = vk::DescriptorType::eCombinedImageSampler;
        sampler_pool_size.descriptorCount = images_.size() * max_samplers;

        // Create the descriptor pool
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            ubo_pool_size, 
            sampler_pool_size
        };
        vk::DescriptorPoolCreateInfo pool_info;
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = images_.size();
        pool_info.poolSizeCount = pool_sizes.size();
        pool_info.pPoolSizes = &pool_sizes[0];

        descriptor_pool_ = logical_->createDescriptorPoolUnique(
            pool_info
        );
    }

    // Create the descriptor sets and map them to the UBOs
    // Descriptor sets can be accessed by a particular shader stage
    void allocate_descriptor_sets() {
        // Reset the pool
        descriptor_sets_.clear();

        // Allocate new descriptor sets within the pool
        std::vector<vk::DescriptorSetLayout> layouts(
            images_.size(), descriptor_layout_.get()
        );
        vk::DescriptorSetAllocateInfo descriptor_alloc_info;
        descriptor_alloc_info.descriptorPool = descriptor_pool_.get();
        descriptor_alloc_info.descriptorSetCount = layouts.size();
        descriptor_alloc_info.pSetLayouts = &layouts[0];

        // How many descriptors do we need for each variable sized set?
        std::vector<uint32_t> descriptor_counts(images_.size(), textures_.size());

        vk::DescriptorSetVariableDescriptorCountAllocateInfo var_descriptor_alloc_info;
        var_descriptor_alloc_info.descriptorSetCount = images_.size();
        var_descriptor_alloc_info.pDescriptorCounts = &descriptor_counts[0];
        descriptor_alloc_info.pNext = &var_descriptor_alloc_info;

        descriptor_sets_ = logical_->allocateDescriptorSetsUnique(
            descriptor_alloc_info
        );
    }

    // Update where the descriptor sets read from
    void write_descriptor_sets() {
        // Map each UBO and texture to a descriptor set
        for(int i = 0; i < uniform_buffer_->get_subbuffer_count(); i++) {
            // Uniform buffer descriptor set
            vk::DescriptorBufferInfo ubo_buffer_info;
            ubo_buffer_info.buffer = uniform_buffer_->get_handle();
            ubo_buffer_info.offset = uniform_buffer_->get_offset(i);
            ubo_buffer_info.range = sizeof(UniformBufferObject);

            vk::WriteDescriptorSet ubo_descriptor_write;
            ubo_descriptor_write.dstSet = descriptor_sets_[i].get();
            ubo_descriptor_write.dstBinding = 0;
            ubo_descriptor_write.dstArrayElement = 0;
            ubo_descriptor_write.descriptorCount = 1;
            ubo_descriptor_write.descriptorType = vk::DescriptorType::eUniformBuffer;
            ubo_descriptor_write.pBufferInfo = &ubo_buffer_info;


            // Image sampler descriptor set
            std::vector<vk::DescriptorImageInfo> image_infos;
            for(auto &texture : textures_) {
                vk::DescriptorImageInfo image_info;
                image_info.sampler = texture_sampler_.get();
                image_info.imageView = texture->get_view();
                image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

                image_infos.push_back(image_info);
            }

            vk::WriteDescriptorSet texture_descriptor_write;
            texture_descriptor_write.dstSet = descriptor_sets_[i].get();
            texture_descriptor_write.dstBinding = 1;
            texture_descriptor_write.dstArrayElement = 0;
            texture_descriptor_write.descriptorCount = static_cast<uint32_t>(textures_.size());
            texture_descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            texture_descriptor_write.pImageInfo = &image_infos[0];


            // Update all descriptor sets
            std::vector<vk::WriteDescriptorSet> descriptor_writes = {
                ubo_descriptor_write, 
                texture_descriptor_write
            };
            logical_->updateDescriptorSets(descriptor_writes, nullptr);
        }
    }

    // Record the commands for each framebuffer
    // This can be called whenever image specific stuff changes
    // But don't call this too frequently for performance
    void record_commands() {
        // Wait for all queues using pool before resetting
        graphics_queue_.waitIdle();
        logical_->resetCommandPool(
            graphics_pool_.get(),
            vk::CommandPoolResetFlagBits::eReleaseResources
        );
        std::array<vk::ClearValue, 2> clear_values = {
            clear_value_, 
            depth_clear_value_
        };

        // Begin recording commands
        vk::CommandBufferBeginInfo begin_info;
        for(int i = 0; i < graphics_commands_.size(); i++) {
            graphics_commands_[i]->begin(begin_info);

            vk::Rect2D render_area;
            render_area.offset.x = 0;
            render_area.offset.y = 0;
            render_area.extent = image_extent_;

            // Start the render pass
            vk::RenderPassBeginInfo render_begin_info;
            render_begin_info.renderPass = render_pass_.get();
            render_begin_info.framebuffer = framebuffers_[i].get();
            render_begin_info.renderArea = render_area;

            render_begin_info.clearValueCount = clear_values.size();
            render_begin_info.pClearValues = &clear_values[0];

            graphics_commands_[i]->beginRenderPass(
                render_begin_info, 
                vk::SubpassContents::eInline
            );

            // Bind the command buffer to the graphics pipeline
            graphics_commands_[i]->bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                pipeline_->get_handle()
            );

            // Draw each mesh
            // TODO: Use secondary buffers for multithreaded rendering
            //       Secondary buffers are hidden from CPU but can be
            //       called by primary command buffers
            for(auto &mesh : mesh_data_) {
                // Bind the vertex and index sub-buffers to the command queue
                std::vector<vk::DeviceSize> offsets = {
                    object_buffer_->get_offset(mesh.vertexes)
                };
                graphics_commands_[i]->bindVertexBuffers(
                    0, object_buffer_->get_handle(), offsets
                );
                graphics_commands_[i]->bindIndexBuffer(
                    object_buffer_->get_handle(), 
                    object_buffer_->get_offset(mesh.indexes), 
                    vk::IndexType::eUint32
                );

                // Bind desccriptor sets
                graphics_commands_[i]->bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics, pipeline_->get_layout(),
                    0, descriptor_sets_[i].get(), nullptr
                );

                // Send push constant data to shader stages
                PushConstantObject push_constant = {
                    mesh.texture
                };
                graphics_commands_[i]->pushConstants(
                    pipeline_->get_layout(), 
                    vk::ShaderStageFlagBits::eVertex,
                    0, 
                    sizeof(push_constant), 
                    &push_constant
                );
                    
                // Draw the mesh
                graphics_commands_[i]->drawIndexed(
                    object_buffer_->get_subfill(mesh.indexes) / sizeof(uint32_t),
                    1, 0, 0, 0
                );
            }

            // Stop recording
            graphics_commands_[i]->endRenderPass();
            graphics_commands_[i]->end();
        }
    }

    // Initialize semaphores and fences to synchronize command buffers
    // * Image available - Image is available for rendering (pre-rendering)
    // * Render finished - Image can be presented to display (post-rendering)
    // * Fences - Wait for queued commands to finish (basically waitIdle)
    void create_synchronizers() {
        vk::SemaphoreCreateInfo semaphore_info;
        vk::FenceCreateInfo fence_info;
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        image_available_signal_.resize(max_frames_processing_);
        render_finished_signal_.resize(max_frames_processing_);
        fences_.resize(max_frames_processing_);
        active_fences_.resize(images_.size());

        for(int i = 0; i < max_frames_processing_; i++) {
            image_available_signal_[i] = std::move(
                logical_->createSemaphoreUnique(semaphore_info)
            );
            render_finished_signal_[i] = std::move(
                logical_->createSemaphoreUnique(semaphore_info)
            );
            fences_[i] = std::move(
                logical_->createFenceUnique(fence_info)
            );
        }
    }

    // Update the uniform buffers every frame
    // This is where we update view and projection matrices
    void update_uniform_buffer(uint32_t image_index) {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
            current_time - start_time
        ).count();

        // World coordinates (Per object)
        glm::mat4 model = glm::rotate(
            glm::mat4(1.0f), 
            time * glm::radians(60.0f), 
            glm::vec3(0.0f, 0.0f, 1.0f)
        );

        // Camera coordinates (Uniform)
        glm::mat4 view = glm::lookAt(
            glm::vec3(2.0f, 2.0f, 2.0f), 
            glm::vec3(0.0f, 0.0f, 0.0f), 
            glm::vec3(0.0f, 0.0f, 1.0f)
        );

        // 45 deg FOV (Uniform)
        float ratio = 0.0f;
        if(image_extent_.height) {
            ratio = image_extent_.width / static_cast<float>(image_extent_.height);
        }
        glm::mat4 proj = glm::perspective(
            glm::radians(45.0f), ratio, 1.0f, 10.0f
        );

        // Vertically flip the projection so the model isn't upside down
        proj[1][1] *= -1;
        
        // Overwrite currently written UBO
        UniformBufferObject ubo = {
            proj * view * model
        };
        uniform_buffer_->clear(image_index);
        uniform_buffer_->copy(image_index, &ubo, sizeof(ubo));
    }

    // Reset the swapchain on changes in window size
    void reset_swapchain() {
        logical_->waitIdle();

        // Do not recreate swapchain if minimized
        auto supported = physical_->get_swapchain_support();
        auto extent = get_swapchain_extent(supported.capabilities);
        if(!extent.width || !extent.height) {
            return;
        }

        // Clear array objects
        images_.clear();
        views_.clear();
        framebuffers_.clear();

        // Recreate swapchain and its dependents
        swapchain_.reset();
        try {
            create_swapchain();
            create_views();
            
            create_depth_image();
            create_depth_view();
            
            create_color_image();
            create_color_view();

            create_render_pass();
            create_graphics_pipeline();

            create_framebuffers();

            record_commands();
        }
        catch(vk::SystemError &err) {
            std::cerr << "Vulkan SystemError: " << err.what() << "\n";
            exit(-1);
        }
    }

    // Reset all descriptor sets
    void reset_descriptor_sets() {
        logical_->waitIdle();
        allocate_descriptor_sets();
        write_descriptor_sets();
        record_commands();
    }

public:
    Renderer(SDL_Window *window) {
        window_ = window;
        max_frames_processing_ = 3;
        buffer_size_ = 1048576; // Initial buffer size (allocate large)
        current_frame_ = 0;

        clear_value_.color.setFloat32({0, 0, 0, 1});
        depth_clear_value_.setDepthStencil({1, 0});
        
        mip_levels_ = 5;

        // Perform all initialization steps
        try {
            get_extensions();
            create_instance();
            create_surface();

            create_physical_device();
            create_logical_device();
            create_swapchain();
            create_views();

            create_depth_image();
            create_depth_view();

            create_color_image();
            create_color_view();

            create_descriptor_layout();
            create_render_pass();
            create_graphics_pipeline();
            
            create_framebuffers();
            create_command_pool();
            create_command_buffers();

            create_object_buffer();
            create_uniform_buffer();

            create_descriptor_pool();
            create_texture_sampler();

            // Load a default white texture
            unsigned char white[] = {255, 255, 255, 255};
            load_texture(white, 1, 1);
            allocate_descriptor_sets();
            write_descriptor_sets();
            
            record_commands();

            create_synchronizers();
        }
        catch(vk::SystemError &err) {
            std::cerr << "Vulkan SystemError: " << err.what() << "\n";
            exit(-1);
        }
    }

    ~Renderer() {
        // Wait for logical device to finish all operations
        logical_->waitIdle();
        debugger_.reset();
    }

    // Update the display
    void refresh() {
        vk::Result result;
        result = logical_->waitForFences(
            fences_[current_frame_].get(), 
            true, 
            UINT64_MAX
        );

        // Grab the next available image to render to
        uint32_t image_index;
        result = logical_->acquireNextImageKHR(
            swapchain_.get(),
            UINT64_MAX,
            image_available_signal_[current_frame_].get(), // Signal that a new frame is available
            nullptr, 
            &image_index
        );
        if(result == vk::Result::eErrorOutOfDateKHR) {
            reset_swapchain();
            return;
        }
        else if(result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Could not acquire image from the swapchain.");
        }
        update_uniform_buffer(image_index);

        if(active_fences_[image_index]) {
            result = logical_->waitForFences(
                active_fences_[image_index],
                true, 
                UINT64_MAX
            );
        }
        active_fences_[image_index] = fences_[current_frame_].get();
        logical_->resetFences(active_fences_[image_index]);

        // Submit commands to the graphics queue
        // for rendering to that image
        vk::PipelineStageFlags wait_stages[] = {
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        };

        vk::SubmitInfo submit_info;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available_signal_[current_frame_].get();
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &graphics_commands_[image_index].get();
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished_signal_[current_frame_].get();
        
        graphics_queue_.submit(
            submit_info, 
            fences_[current_frame_].get() // Signal current frame fence commands are executed
        );

        // Present rendered image to the display!
        // After presenting, wait for next ready image
        vk::PresentInfoKHR present_info;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished_signal_[current_frame_].get();
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_.get();
        present_info.pImageIndices = &image_index;

        // If this fails, we probably need to reset the swapchain
        try {
            result = present_queue_.presentKHR(present_info);

            // Sometimes, it does not throw an error
            if(result != vk::Result::eSuccess) {
                reset_swapchain();
            }
        } 
        catch(vk::OutOfDateKHRError e) {
            reset_swapchain();
        }
        current_frame_++;
        current_frame_ %= max_frames_processing_;
    }

    void set_vsync(bool vsync) {
        vsync_ = vsync;
    }

    bool get_vsync() {
        return vsync_;
    }

    // Set the background fill color
    void set_fill(int r, int g, int b, int a) {
        clear_value_.color.setFloat32({
            r/255.0f, 
            g/255.0f, 
            b/255.0f, 
            a/255.0f
        });
        record_commands();
    }

    // Testing dynamic subbuffer appending
    // TODO: In practice, we will also record a secondary command buffer in a new thread that draws this new mesh
    // ALGORITHM FOR RENDERING ANY MESH
    // * Allocate new subbuffers for the vertex and index data (keep track of an array of these subbuffers)
    // * Record a new secondary command buffer that renders this mesh
    // * At the end of each frame (right before render call), merge secondary buffers with primary
    // * Re-record primary command buffer and submit to queue
    // * Clear the index and staging buffers

    // Alternative? Record secondary buffer ONLY when something new is added
    void add_mesh(Model &model, Texture texture) {
        int index_len_bytes = sizeof(model.indices[0]) * model.indices.size();
        int vertex_len_bytes = sizeof(model.vertices[0]) * model.vertices.size();

        // Copy the index data
        SubBuffer indices = object_buffer_->suballoc(index_len_bytes);
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &model.indices[0], index_len_bytes);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            index_len_bytes, 
            0, 
            indices
        );    

        // Copy the vertex data
        SubBuffer vertices = object_buffer_->suballoc(vertex_len_bytes);
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &model.vertices[0], vertex_len_bytes);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            vertex_len_bytes, 
            0, 
            vertices
        );

        // TODO: Add custom texture
        // Give each mesh its own descriptor set
        mesh_data_.push_back({
            vertices,
            indices,
            texture
        });
        record_commands();
    }

    // Testing dynamic subbuffer removal
    void remove_mesh() {
        if(mesh_data_.empty()) {
            return;
        }
        MeshData mesh = mesh_data_.back();
        object_buffer_->delete_subbuffer(mesh.indexes);
        object_buffer_->delete_subbuffer(mesh.vertexes);
        mesh_data_.pop_back();
        record_commands();
    }

    // Load a texture
    Texture load_texture(std::string filename) {
        int width, height, channels;
        stbi_uc *pixels = stbi_load(
            filename.c_str(), 
            &width, &height, &channels, 
            STBI_rgb_alpha
        );
        Texture texture = load_texture(pixels, width, height);
        stbi_image_free(pixels);
        return texture;
    }

    Texture load_texture(unsigned char pixels[], int width, int height) {
        vk::DeviceSize image_size = width * height * 4;
        if(!pixels) {
            throw std::runtime_error("Could not load image.");
        }
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, pixels, image_size);
        textures_.push_back(
            std::move(
                std::make_unique<TextureData>(
                    logical_.get(), 
                    *physical_.get(),
                    graphics_pool_.get(),
                    graphics_queue_,
                    *staging_buffer_.get(),
                    width, 
                    height,
                    std::floor(std::log2(std::max(width, height))) + 1
                )
            )
        );
        staging_buffer_->clear(0);
        reset_descriptor_sets();
        return textures_.size() - 1;
    } 
};

#endif