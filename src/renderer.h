#ifndef RENDERER_H_
#define RENDERER_H_
#define VULKAN_HPP_TYPESAFE_CONVERSION
#define STB_IMAGE_IMPLEMENTATION
#define GLM_FORCE_RADIANS

#include <vulkan/vulkan.hpp>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "assets/stb_image.h"

#include <chrono>

#include <vector>

#include <exception>
#include <iostream>
#include <fstream>
#include <memory>

#include "physical.h"
#include "texture.h"
#include "commands.h"
#include "buffer.h"
#include "debug.h"
#include "util.h"

#ifndef NDEBUG
    #define DEBUG true
#else
    #define DEBUG false
#endif

using ShaderBytes = std::vector<char>; // Vulkan shader bytecode for parsing

struct RVertex {
    // Interleaved vertex attributes
    glm::vec2 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription get_binding_description() {
        vk::VertexInputBindingDescription desc(
            0,              // Index in array of bindings
            sizeof(RVertex)  // Stride (memory buffer traversal)
        );
        return desc;
    }

    static std::array<vk::VertexInputAttributeDescription, 2> get_attribute_descriptions() {
        std::array<vk::VertexInputAttributeDescription, 2> desc;
        desc[0] = {
            0, 0, 
            vk::Format::eR32G32Sfloat, 
            offsetof(RVertex, pos)   // Memory offset of position member
        };
        desc[1] = {
            1, 0, 
            vk::Format::eR32G32B32Sfloat, 
            offsetof(RVertex, color) // Memory offset of color member
        };
        return desc;
    }
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

// TODO: Implement better command buffer management
// Idea: Create classes of command pools (static/dynamic)
class Renderer {
    std::vector<RVertex> vertices_ = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };
    std::vector<uint16_t> indices_ = {
        0, 1, 2
    };

    SDL_Window *window_;

    // Required extensions and validation layers
    std::vector<const char *> extensions_;
    std::vector<const char *> layers_;

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

    // Image meta data
    vk::Extent2D image_extent_;
    vk::Format image_format_;

    // Descriptor set
    vk::UniqueDescriptorSetLayout descriptor_layout_;
    vk::UniqueDescriptorPool descriptor_pool_;
    std::vector<vk::UniqueDescriptorSet> descriptor_sets_;

    // Graphics pipeline
    vk::UniqueRenderPass render_pass_;
    vk::UniquePipelineLayout layout_;
    vk::UniquePipeline pipeline_;

    // Framebuffers
    std::vector<vk::UniqueFramebuffer> framebuffers_;

    // Command pool (memory buffer for commands)
    vk::UniqueCommandPool graphics_pool_;
    vk::UniqueCommandPool transfer_pool_;

    // Command buffer (recording commands)
    std::vector<vk::UniqueCommandBuffer> graphics_commands_;
    vk::UniqueCommandBuffer single_command_; // For copying

    // Command Queues (submitting commands)
    AvailableQueues queues_;
    vk::Queue graphics_queue_;
    vk::Queue present_queue_;
    vk::Queue transfer_queue_;

    // Data buffers
    // Object buffer is a one-size-fits-all for vertex and index data
    std::unique_ptr<RenderBuffer> staging_buffer_;
    std::unique_ptr<RenderBuffer> object_buffer_;
    SubBuffer index_subbuffer_, vertex_subbuffer_;
    size_t buffer_size_;

    // Uniform buffers
    std::unique_ptr<RenderBuffer> uniform_buffer_;

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

    // Get all required Vulkan extensions from SDL
    void get_extensions() {
        unsigned int count;
        SDL_Vulkan_GetInstanceExtensions(window_, &count, nullptr);

        extensions_.resize(count);
        SDL_Vulkan_GetInstanceExtensions(window_, &count, &extensions_[0]);

        if constexpr(DEBUG) {
            layers_.push_back("VK_LAYER_KHRONOS_validation");
            extensions_.push_back("VK_EXT_debug_utils");
            std::cerr << "Vulkan Extensions:\n";
            for(auto &extension : extensions_) {
                std::cerr << "* " << extension << "\n";
            }
        }
    }

    // Check if the system supports validation layers
    bool is_supporting_layers() {
        auto layer_properties = vk::enumerateInstanceLayerProperties();
        for(auto &requested : layers_) {
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
        vk::ApplicationInfo app_info(
            SDL_GetWindowTitle(window_), // Application name
            VK_MAKE_VERSION(1, 0, 0),    // Application version
            "Dynamo-Engine",             // Engine name
            VK_MAKE_VERSION(1, 0, 0),    // Engine version
            VK_MAKE_VERSION(1, 1, 0)     // Vulkan API version
        );

        std::vector<vk::ValidationFeatureEnableEXT> layer_extensions = {
            vk::ValidationFeatureEnableEXT::eBestPractices,
            vk::ValidationFeatureEnableEXT::eDebugPrintf
        };

        vk::ValidationFeaturesEXT features(
            layer_extensions.size(),
            &layer_extensions[0]
        );

        vk::InstanceCreateInfo create_info(
            vk::InstanceCreateFlags(), 
            &app_info,
            layers_.size(),
            &layers_[0],       // Validation layers
            extensions_.size(),
            &extensions_[0]    // Required extensions
        );
        if constexpr(DEBUG) {
            create_info.pNext = &features;        
        }
        instance_ = vk::createInstanceUnique(create_info);
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
        if constexpr(DEBUG) {
            std::cerr << "Physical Devices:\n";
            for(auto &physical : devices) {
                auto properties = physical.getProperties();
                std::cerr << "* " << properties.deviceName << "\n";
            }
        }

        PhysicalDevice best(devices[0], surface_.get());
        for(auto &physical : devices) {
            PhysicalDevice card(physical, surface_.get());
            if(card.get_score() > best.get_score()) {
                best = card;
            }
        }
        if(!best.get_score()) {
            throw std::runtime_error("Vulkan could not find suitable GPU.");
        }

        physical_ = std::make_unique<PhysicalDevice>(best);
    }

    // Create the logical device from the chosen physical device
    void create_logical_device() {
        auto physical_handle = physical_->get_handle();
        queues_ = physical_->get_available_queues();

        auto cmp = [](QueueFamily a, QueueFamily b) { 
            return a.index != b.index;
        };
        std::set<QueueFamily, decltype(cmp)> unique_families(cmp);
        unique_families.insert(queues_.graphics);
        unique_families.insert(queues_.present);
        unique_families.insert(queues_.transfer);

        // Allocate queues
        std::vector<vk::DeviceQueueCreateInfo> queue_info;
        for(auto &family : unique_families) {
            std::vector<float> priorities(family.count, 0.0f);
            vk::DeviceQueueCreateInfo info(
                vk::DeviceQueueCreateFlags(),
                family.index, family.count, 
                &priorities[0]
            );
            queue_info.push_back(info);
        }

        auto &device_extensions = physical_->get_extensions();
        vk::DeviceCreateInfo create_info(
            vk::DeviceCreateFlags(),
            queue_info.size(),
            &queue_info[0],
            0, nullptr,
            device_extensions.size(),
            &device_extensions[0],
            nullptr
        );
        logical_ = physical_handle.createDeviceUnique(create_info);
        
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

    // Get the extent of the swapchain (i.e., the dimensions of the view)
    vk::Extent2D get_swapchain_extent(const 
                     vk::SurfaceCapabilitiesKHR &supported) {
        vk::Extent2D extent;
        int width, height;
        SDL_Vulkan_GetDrawableSize(window_, &width, &height);

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
    vk::SurfaceFormatKHR get_swapchain_format(const 
                             std::vector<vk::SurfaceFormatKHR> &supported) {
        for(auto &format : supported) {
            if(format.format == vk::Format::eB8G8R8A8Srgb && 
               format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }
        return supported[0];
    }

    // Choose how the swapchain presents images to surface
    // Vsync or no?
    vk::PresentModeKHR get_swapchain_presentation(const 
                           std::vector<vk::PresentModeKHR> &supported) {
        for(auto &mode : supported) {
            if(mode == vk::PresentModeKHR::eMailbox) {
                return mode;
            }
        }
        return vk::PresentModeKHR::eFifo;
    }

    // Create the swapchain and generate its images
    // Swapchain is the collection of images to be worked with
    // Handles presentation (single, double, or tripple buffering?)
    void create_swapchain() {
        auto supported = physical_->get_swapchain_support();

        auto extent = get_swapchain_extent(supported.capabilities);
        auto format = get_swapchain_format(supported.formats);
        auto presentation = get_swapchain_presentation(
            supported.presents
        );

        uint32_t image_count = supported.capabilities.minImageCount + 1;
        if(supported.capabilities.maxImageCount) {
            image_count = std::min(
                image_count,
                supported.capabilities.maxImageCount
            );
        }

        vk::SwapchainCreateInfoKHR create_info(
            vk::SwapchainCreateFlagsKHR(),
            surface_.get(),
            image_count,
            format.format,
            format.colorSpace,
            extent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            supported.capabilities.currentTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            presentation,
            true,
            nullptr
        );

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
            create_info.imageSharingMode = vk::SharingMode::eConcurrent;
            create_info.queueFamilyIndexCount = index_arr.size();
            create_info.pQueueFamilyIndices = &index_arr[0];
        }

        swapchain_ = logical_->createSwapchainKHRUnique(create_info);
        images_ = logical_->getSwapchainImagesKHR(swapchain_.get());

        // Store image meta data for rendering pipeline
        image_extent_ = extent;
        image_format_ = format.format;
    }

    // Create views to each swapchain image
    // Views are simply references to a specific part of an image
    void create_views() {
        vk::ComponentMapping components(
            vk::ComponentSwizzle::eR, 
            vk::ComponentSwizzle::eG, 
            vk::ComponentSwizzle::eB, 
            vk::ComponentSwizzle::eA
        );
        vk::ImageSubresourceRange subresource_range(
            vk::ImageAspectFlagBits::eColor, 
            0, 1, // Mipmaps 
            0, 1  // Layers
        );

        for(auto &image : images_) {
            vk::ImageViewCreateInfo create_info(
                vk::ImageViewCreateFlags(),
                image,
                vk::ImageViewType::e2D, // Is the display gonna be 3D or 2D?
                image_format_,
                components,
                subresource_range
            );
            views_.push_back(
                logical_->createImageViewUnique(create_info)
            );
        }
    }

    // Create the descriptor set layout
    // Describes the data passed to a shader stage
    // Can add more stuff for dynamic shader behavior
    void create_descriptor_layout() {
        vk::DescriptorSetLayoutBinding binding(
            0, vk::DescriptorType::eUniformBuffer, 1,
            vk::ShaderStageFlagBits::eVertex
        );

        vk::DescriptorSetLayoutCreateInfo create_info(
            vk::DescriptorSetLayoutCreateFlags(),
            1, &binding
        );
        descriptor_layout_ = logical_->createDescriptorSetLayoutUnique(
            create_info
        );
    }

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
        vk::ShaderModuleCreateInfo create_info(
            vk::ShaderModuleCreateFlags(),
            code.size(),
            reinterpret_cast<uint32_t *>(&code[0])
        );
        return logical_->createShaderModuleUnique(
            create_info
        );
    }

    // Initialize the render pass
    // Describes the scope of a render operation (color, depth, stencil)
    // Lists attachments, dependencies, and subpasses
    // Driver knows what to expect for render operation
    void create_render_pass() {
        // Color buffer for a single swapchain image
        vk::AttachmentDescription color_attachment(
            vk::AttachmentDescriptionFlags(),
            image_format_,                  // Pixel format of image
            vk::SampleCountFlagBits::e1,    // No. of Samples

            vk::AttachmentLoadOp::eClear,   // Clear buffer before rendering
            vk::AttachmentStoreOp::eStore,  // Keep data after rendering

            // Stencil buffer (unused, so it doesn't matter)
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,

            vk::ImageLayout::eUndefined,    // Initial layout
            vk::ImageLayout::ePresentSrcKHR // Final layout
        );

        // 1 Subpass
        vk::AttachmentReference color_ref(
            0, vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::SubpassDescription subpass(
            vk::SubpassDescriptionFlags(),
            vk::PipelineBindPoint::eGraphics,
            0, nullptr,
            1, &color_ref,
            0, nullptr
        );

        // Create the render pass
        vk::RenderPassCreateInfo render_pass_info(
            vk::RenderPassCreateFlags(),
            1, &color_attachment,
            1, &subpass
        );
        render_pass_ = logical_->createRenderPassUnique(render_pass_info);
    }

    // Initialize all stages of the graphics pipeline
    // This determines what and how things are drawn
    // This is the heart of the Renderer, fixed at runtime
    // We can have multiple pipelines for different types of rendering
    // TODO: Figure out how to make other graphics pipelines
    //       This is specialized for triangle drawing.
    //       Abstract this into its own class maybe?
    void create_graphics_pipeline() {
        // Load all required shaders
        auto vert = create_shader(load_shader("vert.spv")); // Vertex shader (transforms)
        auto frag = create_shader(load_shader("frag.spv")); // Fragment shader (color/depth)
        
        // Create the shader stages
        vk::PipelineShaderStageCreateInfo vert_info(
            vk::PipelineShaderStageCreateFlags(),
            vk::ShaderStageFlagBits::eVertex,
            vert.get(), "main"
        );
        vk::PipelineShaderStageCreateInfo frag_info(
            vk::PipelineShaderStageCreateFlags(),
            vk::ShaderStageFlagBits::eFragment,
            frag.get(), "main"
        );
        std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {
            vert_info,
            frag_info
        };

        // Create the fixed function stages
        // Vertex input stage
        // These describe the data that will be placed in the buffers
        auto binding_description = RVertex::get_binding_description();
        auto attribute_descriptions = RVertex::get_attribute_descriptions();
        vk::PipelineVertexInputStateCreateInfo vertex_input_info(
            vk::PipelineVertexInputStateCreateFlags(),
            1, 
            &binding_description, 
            attribute_descriptions.size(), 
            &attribute_descriptions[0]
        );

        // Input assembly stage
        // Change this for different shapes! (This one is for triangles)
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_info(
            vk::PipelineInputAssemblyStateCreateFlags(),
            vk::PrimitiveTopology::eTriangleList,
            false
        );

        // Viewport creation stage (region of the image to be drawn to)
        vk::Viewport viewport = {
            0, 0, // Position of origin
            static_cast<float>(image_extent_.width), 
            static_cast<float>(image_extent_.height), 
            0, 1  // Depth Range 
        };
        vk::Rect2D scissor = { // Excluded region is cropped from image
            {0, 0},
            image_extent_ 
        };
        vk::PipelineViewportStateCreateInfo viewport_info(
            vk::PipelineViewportStateCreateFlags(),
            1, &viewport,
            1, &scissor
        );

        // Rasterization stage (converts vectors to pixels)
        vk::PipelineRasterizationStateCreateInfo rasterizer_info(
            vk::PipelineRasterizationStateCreateFlags(),
            false, // Depth clamp enable (shadow maps)
            false, // Always set to false if we wanna draw
            vk::PolygonMode::eFill,           // Fill, outline, or only points?
            vk::CullModeFlagBits::eBack,      // Cull the back (hidden) faces
            vk::FrontFace::eCounterClockwise, // Order of reading vertices
            false, 0.0f, 0.0f, 0.0f,          // Depth biases for (shadow maps)
            1.0f   // Line thickness
        );

        // Multisampling stage (anti-aliasing)
        // TODO: Revisit this and reduce fuzzy edges
        vk::PipelineMultisampleStateCreateInfo multisampler_info(
            vk::PipelineMultisampleStateCreateFlags(),
            vk::SampleCountFlagBits::e1
        );

        // Create the color blender attachment for the blending stage
        // Use this for custom blending functions
        // if(enable blend) {
        //     final.rgb = (src_f * new.rgb) <color_op> (dst_f * old.rgb);
        //     final.a = (src_f * new.a) <alpha_op> (dst_f * old.a);
        // } 
        // else {
        //     final = new;
        // }
        vk::ColorComponentFlags color_components(
            vk::ColorComponentFlagBits::eR | 
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | 
            vk::ColorComponentFlagBits::eA
        );
        vk::PipelineColorBlendAttachmentState blender_attachment(
            true, // Enable blending
            
            // Default is alpha blending
            vk::BlendFactor::eSrcAlpha,         // color src blend factor
            vk::BlendFactor::eOneMinusSrcAlpha, // color dst blend factor
            vk::BlendOp::eAdd,                  // color blend operation

            vk::BlendFactor::eOne,  // alpha src blend factor
            vk::BlendFactor::eZero, // alpha dst blend factor
            vk::BlendOp::eAdd,      // alpha blend operation
            
            color_components
        );

        // Blending stage
        vk::PipelineColorBlendStateCreateInfo blend_info(
            vk::PipelineColorBlendStateCreateFlags(),
            false, // Disable bitwise blending 
            vk::LogicOp::eNoOp,
            1,
            &blender_attachment
        );

        // Enumerate changeable dynamic (runtime) states
        // TODO: Make these changeable at runtime
        std::vector<vk::DynamicState> dynamic_states = {
            vk::DynamicState::eViewport,      // Change size of display
            vk::DynamicState::eLineWidth,     // Change width of line drawing
            vk::DynamicState::eBlendConstants // Change blending function
        };
        vk::PipelineDynamicStateCreateInfo dynamic_info {
            vk::PipelineDynamicStateCreateFlags(),
            0, nullptr
        };

        // Define pipeline layout
        vk::PipelineLayoutCreateInfo layout_info(
            vk::PipelineLayoutCreateFlags(),
            1, &descriptor_layout_.get(), 0, nullptr
        );
        layout_ = logical_->createPipelineLayoutUnique(layout_info);

        // Create the pipeline, assemble all the stages
        vk::GraphicsPipelineCreateInfo pipeline_info(
            vk::PipelineCreateFlags(),
            shader_stages.size(), &shader_stages[0],
            &vertex_input_info,
            &input_assembly_info,
            nullptr,            // Tesselation stage (unused)
            &viewport_info,
            &rasterizer_info,
            &multisampler_info,
            nullptr,            // Depth stencil stage (unused)
            &blend_info,
            &dynamic_info,
            layout_.get(),
            render_pass_.get(), // Can use other (compatible) render passes
            0,                  // Subpass index
            nullptr, -1         // Base pipeline handle/index
        );
        pipeline_ = logical_->createGraphicsPipelineUnique(
            nullptr, pipeline_info
        );
    }

    // Create the framebuffers for each swapchain image
    // Framebuffers hold the memory attachments used by render pass
    // Ex. color image buffer and depth buffer
    void create_framebuffers() {
        for(auto &view : views_) {
            vk::FramebufferCreateInfo framebuffer_info(
                vk::FramebufferCreateFlags(),
                render_pass_.get(),
                1, &view.get(),
                image_extent_.width,
                image_extent_.height,
                1
            );
            auto framebuffer = logical_->createFramebufferUnique(
                framebuffer_info
            );
            framebuffers_.push_back(std::move(framebuffer));
        }
    }

    // Create the command pools that manage command buffers
    // for each device queue family
    void create_command_pool() {
        // Commands for the graphics queue
        vk::CommandPoolCreateInfo graphics_info(
            vk::CommandPoolCreateFlags(),
            queues_.graphics.index
        );
        graphics_pool_ = logical_->createCommandPoolUnique(graphics_info);

        // Commands for the transfer queue
        vk::CommandPoolCreateInfo transfer_info(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            queues_.transfer.index
        );
        transfer_pool_ = logical_->createCommandPoolUnique(transfer_info);
    }

    // Allocate buffers for submitting commands
    void create_command_buffers() {
        vk::CommandBufferAllocateInfo alloc_info(
            graphics_pool_.get(),
            vk::CommandBufferLevel::ePrimary,
            framebuffers_.size()
        );
        graphics_commands_ = logical_->allocateCommandBuffersUnique(
            alloc_info
        );

        // Create a command buffer for copying between data buffers
        // This is not attached to any pipeline stage or semaphores
        vk::CommandBufferAllocateInfo copy_alloc_info(
            transfer_pool_.get(),
            vk::CommandBufferLevel::ePrimary,
            1
        );
        single_command_ = std::move(
            logical_->allocateCommandBuffersUnique(copy_alloc_info)[0]
        );

        // Create the staging buffer for CPU to GPU copies
        staging_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, logical_.get(), *physical_, 
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | 
            vk::MemoryPropertyFlagBits::eHostCoherent |
            vk::MemoryPropertyFlagBits::eHostCached,
            single_command_.get(), transfer_queue_
        );
        staging_buffer_->suballoc(buffer_size_);
    }

    // Create the object buffer
    void create_object_buffer() {
        auto usage = vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eVertexBuffer;
        object_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, logical_.get(), *physical_,
            usage, vk::MemoryPropertyFlagBits::eDeviceLocal,
            single_command_.get(), transfer_queue_
        );

        // Copy the index data
        int index_len = sizeof(indices_[0]) * indices_.size();
        int vertex_len = sizeof(vertices_[0]) * vertices_.size();


        index_subbuffer_ = object_buffer_->suballoc(index_len);
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &indices_[0], index_len);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            index_len, 
            0, 
            index_subbuffer_
        );    

        // Copy the vertex data
        vertex_subbuffer_ = object_buffer_->suballoc(vertex_len);
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &vertices_[0], vertex_len);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            vertex_len, 
            0, 
            vertex_subbuffer_
        );
    }

    // Create a uniform buffer per swapchain image
    void create_uniform_buffer() {
        uniform_buffer_ = std::make_unique<RenderBuffer>(
            buffer_size_, logical_.get(), *physical_, 
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent |
            vk::MemoryPropertyFlagBits::eHostCached,
            single_command_.get(), transfer_queue_
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
        vk::DescriptorPoolSize size(
            vk::DescriptorType::eUniformBuffer,
            images_.size()
        );

        vk::DescriptorPoolCreateInfo create_info(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            images_.size(),
            1, &size
        );
        descriptor_pool_ = logical_->createDescriptorPoolUnique(
            create_info
        );
    }

    // Create the descriptor sets and map them to the UBOs
    // Descriptor sets can be accessed by a particular shader stage
    void create_descriptor_sets() {
        // Allocate the descriptor sets within the pool
        std::vector<vk::DescriptorSetLayout> layouts(
            images_.size(), descriptor_layout_.get()
        );
        vk::DescriptorSetAllocateInfo alloc_info(
            *descriptor_pool_,
            layouts.size(),
            &layouts[0]
        );
        descriptor_sets_ = logical_->allocateDescriptorSetsUnique(
            alloc_info
        );

        // Map each UBO in uniform_buffer_ to a descriptor set
        size_t unit = sizeof(UniformBufferObject);
        for(int i = 0; i < uniform_buffer_->get_subbuffer_count(); i++) {
            vk::DescriptorBufferInfo buffer_info(
                uniform_buffer_->get_handle(), 
                uniform_buffer_->get_offset(i), unit
            );
            vk::WriteDescriptorSet write_info(
                descriptor_sets_[i].get(),
                0, // Binding value in shader layout 
                0, 1,
                vk::DescriptorType::eUniformBuffer,
                nullptr, &buffer_info, nullptr   
            );
            logical_->updateDescriptorSets(write_info, nullptr);
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

        // Begin recording commands
        vk::CommandBufferBeginInfo begin_info;
        for(int i = 0; i < graphics_commands_.size(); i++) {
            graphics_commands_[i]->begin(begin_info);

            vk::RenderPassBeginInfo render_begin_info(
                render_pass_.get(),
                framebuffers_[i].get(),
                vk::Rect2D({0, 0}, image_extent_),
                1, &clear_value_
            );
            graphics_commands_[i]->beginRenderPass(
                render_begin_info, 

                // Render pass commands embedded on primary command buffer
                // TODO: Use secondary buffers for multithreaded rendering
                //       Secondary buffers are hidden from CPU but can be
                //       called by primary command buffers
                vk::SubpassContents::eInline
            );

            // Bind the buffer to the graphics pipeline
            graphics_commands_[i]->bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                pipeline_.get()
            );

            // Bind the vertex and index sub-buffers to the command queue
            std::vector<vk::DeviceSize> offsets = {
                object_buffer_->get_offset(vertex_subbuffer_)
            };
            graphics_commands_[i]->bindVertexBuffers(
                0, object_buffer_->get_handle(), offsets
            );
            graphics_commands_[i]->bindIndexBuffer(
                object_buffer_->get_handle(), 
                object_buffer_->get_offset(index_subbuffer_), 
                vk::IndexType::eUint16
            );

            // Bind desccriptor sets
            std::vector<vk::DescriptorSet> bound_descriptors = {
                descriptor_sets_[i].get()
            };
            graphics_commands_[i]->bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, layout_.get(),
                0, bound_descriptors, nullptr
            );

            // Record the actual draw command!!!!
            graphics_commands_[i]->drawIndexed(
                object_buffer_->get_subfill(index_subbuffer_) / sizeof(uint16_t),
                1, 0, 0, 0
            );

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
        vk::FenceCreateInfo fence_info(
            vk::FenceCreateFlagBits::eSignaled
        );

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
    void update_uniform_buffer(uint32_t image_index) {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
            current_time - start_time
        ).count();

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), 
                                glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), 
                               glm::vec3(0.0f, 0.0f, 1.0f));
        float ratio = 0.0f;
        if(image_extent_.height) {
            ratio = image_extent_.width / static_cast<float>(image_extent_.height);
        }
        ubo.proj = glm::perspective(
            glm::radians(45.0f), ratio, 1.0f, 10.0f
        );
        ubo.proj[1][1] *= -1;

        // Overwrite currently written UBO
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

public:
    Renderer(SDL_Window *window) {
        window_ = window;
        max_frames_processing_ = 3;
        buffer_size_ = 1048576; // Initial buffer size (allocate large)
        current_frame_ = 0;

        clear_value_.color.setFloat32({0, 0, 0, 1});

        // Perform all initialization steps
        try {
            get_extensions();
            create_instance();
            create_surface();

            create_physical_device();
            create_logical_device();
            create_swapchain();
            create_views();
            
            create_descriptor_layout();
            create_render_pass();
            create_graphics_pipeline();
            
            create_framebuffers();
            create_command_pool();
            create_command_buffers();

            create_object_buffer();
            create_uniform_buffer();

            create_descriptor_pool();
            create_descriptor_sets();
            
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
        logical_->waitForFences(
            fences_[current_frame_].get(), 
            true, UINT64_MAX
        );

        // Grab the next available image to render to
        uint32_t image_index;
        vk::Result result = logical_->acquireNextImageKHR(
            swapchain_.get(),
            UINT64_MAX,
            // Signal that a new frame is available
            image_available_signal_[current_frame_].get(),
            nullptr, &image_index
        );
        if(result == vk::Result::eSuboptimalKHR || 
           result == vk::Result::eErrorOutOfDateKHR) {
            reset_swapchain();
            return;
        }
        update_uniform_buffer(image_index);

        if(active_fences_[image_index]) {
            logical_->waitForFences(
                active_fences_[image_index],
                true, UINT64_MAX
            );
        }
        active_fences_[image_index] = fences_[current_frame_].get();
        logical_->resetFences(active_fences_[image_index]);

        // Submit commands to the graphics queue
        // for rendering to that image
        vk::PipelineStageFlags wait_stages[] = {
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        };
        vk::SubmitInfo submit_info(
            // Wait for the image to become available
            1, &image_available_signal_[current_frame_].get(),
            wait_stages,
            1, &graphics_commands_[image_index].get(),
            // Signal that rendering is finished and can be presented
            1, &render_finished_signal_[current_frame_].get()
        );
        graphics_queue_.submit(
            submit_info, 
            // Signal current frame fence commands are executed
            fences_[current_frame_].get()
        );

        // Present rendered image to the display!
        // After presenting, wait for next ready image
        vk::PresentInfoKHR present_info(
            // Wait for signal that the image is ready for presentation
            1, &render_finished_signal_[current_frame_].get(),
            1, &swapchain_.get(),
            &image_index, nullptr
        );
        try {
            // For some reason, this throws an exception instead...
            present_queue_.presentKHR(present_info);
            present_queue_.waitIdle();
        }
        catch(vk::OutOfDateKHRError &err) {
            reset_swapchain();
            return;
        }

        current_frame_++;
        current_frame_ %= max_frames_processing_;
    }

    // Set the background fill color
    void set_fill(int r, int g, int b, int a) {
        clear_value_.color.setFloat32(
            {r/255.0f, g/255.0f, b/255.0f, a/255.0f}
        );
        record_commands();
    }

    // Testing dynamic subbuffer appending
    void add_triangle() {
        std::vector<RVertex> vertices = {
            {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
        };
        std::vector<uint16_t> indices = {
            2, 3, 0
        };

        // Copy the index data
        int index_len = sizeof(indices[0]) * indices.size();
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &indices[0], index_len);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            index_len, 
            0, 
            index_subbuffer_
        );
        // Copy the vertex data
        int vertex_len = sizeof(vertices[0]) * vertices.size();
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, &vertices[0], vertex_len);
        staging_buffer_->copy_buffer(
            *object_buffer_, 
            vertex_len, 
            0, 
            vertex_subbuffer_
        );
        record_commands();
    }

    // Testing dynamic subbuffer removal
    void remove_triangle() {
        for(int i = 0; i < 3; i++) {        
            object_buffer_->pop(index_subbuffer_, sizeof(indices_[0]));
        }
        object_buffer_->pop(vertex_subbuffer_, sizeof(vertices_[0]));
        record_commands();
    }

    // Load a texture
    void load_texture(std::string filename) {
        int width, height, channels;
        stbi_uc *pixels = stbi_load(
            filename.c_str(), 
            &width, &height, &channels, 
            STBI_rgb_alpha
        );

        vk::DeviceSize image_size = width * height * 4;
        if(!pixels) {
            throw std::runtime_error("Could not load image.");
        }
        staging_buffer_->clear(0);
        staging_buffer_->copy(0, pixels, image_size);
        stbi_image_free(pixels);
    }
};

#endif