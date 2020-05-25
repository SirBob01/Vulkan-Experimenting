#include <vulkan/vulkan.hpp>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>

#include <vector>

#include <exception>
#include <iostream>
#include <fstream>
#include <memory>

#include "physical.h"
#include "util.h"

#ifndef NDEBUG
    #define DEBUG true
#else
    #define DEBUG false
#endif

using ShaderBytes = std::vector<char>; // Vulkan shader bytecode for parsing

// TODO: Understand these methods
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
            0, 0, vk::Format::eR32G32Sfloat, offsetof(RVertex, pos)
        };
        desc[1] = {
            1, 0, vk::Format::eR32G32B32Sfloat, offsetof(RVertex, color)
        };
        return desc;
    }
};

class RenderBuffer {
    vk::Device logical_;
    vk::UniqueBuffer handle_;
    vk::UniqueDeviceMemory memory_;

    size_t length_;

    // Initialize the buffer handle
    void initialize_buffer(vk::BufferUsageFlags usage) {
        vk::BufferCreateInfo buffer_info(
            vk::BufferCreateFlags(),
            length_, usage
        );
        handle_ = logical_.createBufferUnique(buffer_info);
    }

    // Allocate memory in the GPU for the buffer
    void allocate_memory(vk::MemoryPropertyFlags properties, 
                         vk::PhysicalDeviceMemoryProperties available) {
        auto requirements = logical_.getBufferMemoryRequirements(
            handle_.get()
        );

        int memory_type = -1;
        for(uint32_t i = 0; i < available.memoryTypeCount; i++) {
            if((requirements.memoryTypeBits & (1 << i)) && 
               (available.memoryTypes[i].propertyFlags & properties)) {
                memory_type = i;
                break;
            }
        }
        if(memory_type < 0) {
            throw std::runtime_error("Vulkan failed to create buffer");
        }
        vk::MemoryAllocateInfo alloc_info(
            requirements.size,
            memory_type
        );
        memory_ = logical_.allocateMemoryUnique(
            alloc_info
        );
    }

public:
    RenderBuffer(vk::UniqueDevice &logical, size_t length, 
                 vk::BufferUsageFlags usage, 
                 vk::MemoryPropertyFlags properties,
                 vk::PhysicalDeviceMemoryProperties device_spec) {
        logical_ = logical.get();
        length_ = length;

        initialize_buffer(usage);
        allocate_memory(properties, device_spec);

        // Bind the device memory to the vertex buffer
        logical_.bindBufferMemory(
            handle_.get(), memory_.get(), 0
        );
    }

    // Get the handle to the Vulkan buffer
    vk::Buffer &get_handle() {
        return handle_.get();
    }

    // Copy the data to VRAM
    void copy(void *usr_data) {
        void *bind = logical_.mapMemory(
            memory_.get(), 0, 
            length_
        );
        memcpy(bind, usr_data, length_);
        logical_.unmapMemory(memory_.get());
    }
};


class Renderer {
    std::vector<RVertex> vertices_ = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
    };
    std::vector<uint16_t> indices_ = {
        0, 1, 2, 2, 3, 0
    };

    SDL_Window *window_;

    // Required extensions and validation layers
    std::vector<const char *> extensions_;
    std::vector<const char *> layers_;

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

    // Graphics pipeline
    vk::UniqueRenderPass render_pass_;
    vk::UniquePipelineLayout layout_;
    vk::UniquePipeline pipeline_;

    // Framebuffers
    std::vector<vk::UniqueFramebuffer> framebuffers_;

    // Command pool (memory buffer for commands)
    vk::UniqueCommandPool command_pool_;

    // Command buffer (recording commands)
    std::vector<vk::UniqueCommandBuffer> command_buffers_;

    // Command Queues (submitting commands)
    vk::Queue graphics_queue_;
    vk::Queue present_queue_;

    // Data buffers
    // Index buffer is so we can store indices to reuse vertices
    std::unique_ptr<RenderBuffer> vertex_buffer_;
    std::unique_ptr<RenderBuffer> index_buffer_; 

    // Semaphores
    // Ensures correct ordering between graphic and present commands
    std::vector<vk::UniqueSemaphore> image_available_semaphores_;
    std::vector<vk::UniqueSemaphore> render_finished_semaphores_;

    // Fences
    // Synchronizes between GPU and CPU processes
    std::vector<vk::UniqueFence> wait_fences_;
    std::vector<vk::Fence> active_images_;

    // Frame processing indices
    int max_frames_processing_;
    int current_frame_;

    // Get all required Vulkan extensions from SDL
    void get_extensions() {
        unsigned int count;
        SDL_Vulkan_GetInstanceExtensions(window_, &count, nullptr);

        extensions_.resize(count);
        SDL_Vulkan_GetInstanceExtensions(window_, &count, &extensions_[0]);

        if(DEBUG) {
            layers_.push_back("VK_LAYER_KHRONOS_validation");
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
            VK_MAKE_VERSION(1, 2, 135)   // Vulkan API version
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
        if(DEBUG) {
            create_info.pNext = &features;        
        }
        instance_ = vk::createInstanceUnique(create_info);
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
        if(DEBUG) {
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
        auto queue_families = physical_->get_queue_families();

        std::set<int> unique_families = {
            queue_families.graphics,
            queue_families.present
        };

        // This dictates the order of commands buffers performed
        float priority = 0.0;
        std::vector<vk::DeviceQueueCreateInfo> queue_info;
        for(auto &family : unique_families) {
            vk::DeviceQueueCreateInfo info(
                vk::DeviceQueueCreateFlags(),
                queue_families.graphics,
                1, &priority
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
        
        // Set up the device's command queues
        graphics_queue_ = logical_->getQueue(queue_families.graphics, 0);
        present_queue_  = logical_->getQueue(queue_families.present, 0);
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

        auto queue_families = physical_->get_queue_families();
        uint32_t index_arr[] = {
            static_cast<uint32_t>(queue_families.graphics),
            static_cast<uint32_t>(queue_families.present)
        };
        if(queue_families.graphics != queue_families.present) {
            create_info.imageSharingMode = vk::SharingMode::eConcurrent;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = index_arr;
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
            0,
            vk::ImageLayout::eColorAttachmentOptimal
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
        // TODO: Understand this
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
            vk::PolygonMode::eFill,      // Fill, outline, or only points?
            vk::CullModeFlagBits::eBack, // Cull the back (hidden) faces
            vk::FrontFace::eClockwise,   // Order of reading vertices
            false, 0.0f, 0.0f, 0.0f,     // Depth biases for (shadow maps)
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
            0, nullptr, 0, nullptr
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

    // Create the command pool that manages buffer memory
    void create_command_pool() {
        vk::CommandPoolCreateInfo create_info(
            vk::CommandPoolCreateFlags(),
            physical_->get_queue_families().graphics
        );
        command_pool_ = logical_->createCommandPoolUnique(create_info);
    }

    // Allocate buffers for submitting commands
    void create_command_buffers() {
        vk::CommandBufferAllocateInfo alloc_info(
            command_pool_.get(),
            vk::CommandBufferLevel::ePrimary,
            framebuffers_.size()
        );
        command_buffers_ = std::move(
            logical_->allocateCommandBuffersUnique(alloc_info)
        );
    }

    // Copy from data from one RenderBuffer to another
    // Use for communicating GPU and CPU resources
    void copy_buffer(RenderBuffer &src, RenderBuffer &dst, int size) {
        // Create a command buffer for copying between data buffers
        vk::CommandBufferAllocateInfo alloc_info(
            command_pool_.get(),
            vk::CommandBufferLevel::ePrimary,
            1
        );
        auto copy_command = logical_->allocateCommandBuffersUnique(alloc_info);

        vk::CommandBufferBeginInfo begin_info(
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        );

        // Perform the copy
        copy_command[0]->begin(begin_info);
        vk::BufferCopy copy_region(0, 0, size);
        copy_command[0]->copyBuffer(
            src.get_handle(), dst.get_handle(), 
            1, &copy_region
        );
        copy_command[0]->end();

        // Submit the command to the graphics queue
        vk::SubmitInfo submit_info(
            0, nullptr, nullptr, 
            1, &copy_command[0].get()
        );
        graphics_queue_.submit(submit_info, nullptr);
        graphics_queue_.waitIdle();
    }

    // Create the vertex buffer
    void create_vertex_buffer() {
        int length = sizeof(vertices_[0]) * vertices_.size();
        RenderBuffer staging_buffer(
            logical_, length,
            vk::BufferUsageFlagBits::eTransferSrc,            
            vk::MemoryPropertyFlagBits::eHostVisible | 
            vk::MemoryPropertyFlagBits::eHostCoherent,
            physical_->get_memory()
        );

        staging_buffer.copy(&vertices_[0]);

        vertex_buffer_ = std::make_unique<RenderBuffer>(
            logical_, length,
            vk::BufferUsageFlagBits::eVertexBuffer | 
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            physical_->get_memory()
        );

        copy_buffer(
            staging_buffer,
            *vertex_buffer_,
            length
        );
    }

    // Create the index buffer
    void create_index_buffer() {
        int length = sizeof(indices_[0]) * indices_.size();
        RenderBuffer staging_buffer(
            logical_, length,
            vk::BufferUsageFlagBits::eTransferSrc,            
            vk::MemoryPropertyFlagBits::eHostVisible | 
            vk::MemoryPropertyFlagBits::eHostCoherent,
            physical_->get_memory()
        );

        staging_buffer.copy(&indices_[0]);

        index_buffer_ = std::make_unique<RenderBuffer>(
            logical_, length,
            vk::BufferUsageFlagBits::eIndexBuffer | 
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            physical_->get_memory()
        );

        copy_buffer(
            staging_buffer,
            *index_buffer_,
            length
        );
    }

    // Begin recording commands from each buffer
    void record_commands() {
        // Begin recording commands
        for(int i = 0; i < command_buffers_.size(); i++) {
            vk::CommandBufferBeginInfo begin_info;
            command_buffers_[i]->begin(begin_info);

            // Basically, SDL_RenderClear() 
            std::array<float, 4> black = {
                0.0, 0.0, 0.0, 1.0
            };
            vk::ClearValue clear_value(black);

            vk::RenderPassBeginInfo render_begin_info(
                render_pass_.get(),
                framebuffers_[i].get(),
                vk::Rect2D({0, 0}, image_extent_),
                1, &clear_value
            );
            command_buffers_[i]->beginRenderPass(
                render_begin_info, 

                // Render pass commands embedded on primary command buffer
                // TODO: Use secondary buffers for multithreaded rendering
                vk::SubpassContents::eInline
            );

            // Bind the buffer to the graphics pipeline
            command_buffers_[i]->bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                pipeline_.get()
            );

            // Bind the vertex buffer
            // TODO: Understand this
            vk::DeviceSize offsets[] = {0};
            command_buffers_[i]->bindVertexBuffers(
                0, 1, &vertex_buffer_->get_handle(), offsets
            );
            command_buffers_[i]->bindIndexBuffer(
                index_buffer_->get_handle(), 0, vk::IndexType::eUint16
            );

            // Submit the draw the triangle command!
            // Finally!!!
            command_buffers_[i]->drawIndexed(indices_.size(), 1, 0, 0, 0);

            // Stop recording
            command_buffers_[i]->endRenderPass();
            command_buffers_[i]->end();
        }
    }

    // Initialize semaphores and fences to synchronize command buffers
    // * Image available - Image is available for rendering (pre-rendering)
    // * Render finished - Image can be presented to display (post-rendering)
    // TODO: Understand purpose of various fence objects...???
    void create_synchronizers() {
        vk::SemaphoreCreateInfo semaphore_info;
        vk::FenceCreateInfo fence_info(
            vk::FenceCreateFlagBits::eSignaled
        );

        image_available_semaphores_.resize(max_frames_processing_);
        render_finished_semaphores_.resize(max_frames_processing_);
        wait_fences_.resize(max_frames_processing_);
        active_images_.resize(images_.size());

        for(int i = 0; i < max_frames_processing_; i++) {
            image_available_semaphores_[i] = std::move(
                logical_->createSemaphoreUnique(semaphore_info)
            );
            render_finished_semaphores_[i] = std::move(
                logical_->createSemaphoreUnique(semaphore_info)
            );
            wait_fences_[i] = std::move(
                logical_->createFenceUnique(fence_info)
            );
        }
    }

    // Reset the swapchain on changes in window size
    void reset_swapchain() {
        logical_->waitIdle();

        // Handle minimized window (special case)
        int width = 0, height = 0;
        while(!width || !height) {
            SDL_Vulkan_GetDrawableSize(window_, &width, &height);
        }

        // Clear array objects
        command_buffers_.clear();
        images_.clear();
        views_.clear();
        framebuffers_.clear();

        // Recreate swapchain and its dependents
        try {
            create_swapchain();
            create_views();
            
            create_render_pass();
            create_graphics_pipeline();

            create_framebuffers();
            create_command_buffers();
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
        current_frame_ = 0;

        // Perform all initialization steps
        try {
            get_extensions();
            create_instance();
            create_surface();

            create_physical_device();
            create_logical_device();
            create_swapchain();
            create_views();
            
            create_render_pass();
            create_graphics_pipeline();
            
            create_framebuffers();
            create_command_pool();
            create_command_buffers();

            create_vertex_buffer();
            create_index_buffer();
            
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
    }

    // Update the display
    void refresh() {
        logical_->waitForFences(
            1, &wait_fences_[current_frame_].get(), 
            true, UINT64_MAX
        );

        // Grab the next available image to render to
        uint32_t image_index;
        vk::Result result = logical_->acquireNextImageKHR(
            swapchain_.get(),
            UINT64_MAX,
            image_available_semaphores_[current_frame_].get(),
            nullptr, &image_index
        );
        if(result == vk::Result::eErrorOutOfDateKHR) {
            reset_swapchain();
            return;
        }

        if(active_images_[image_index]) {
            logical_->waitForFences(
                1, &active_images_[image_index],
                true, UINT64_MAX
            );
        }
        active_images_[image_index] = wait_fences_[current_frame_].get();
        logical_->resetFences(1, &wait_fences_[current_frame_].get());

        // Submit commands to the graphics queue
        // for rendering to that image
        vk::PipelineStageFlags wait_stages[] = {
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        };
        vk::SubmitInfo submit_info(
            1, &image_available_semaphores_[current_frame_].get(),
            wait_stages,
            1, &command_buffers_[image_index].get(),
            1, &render_finished_semaphores_[current_frame_].get()
        );
        graphics_queue_.submit(
            submit_info, 
            wait_fences_[current_frame_].get()
        );

        // Present rendered image to the display!
        // After presenting, wait for next ready image
        vk::PresentInfoKHR present_info(
            1, &render_finished_semaphores_[current_frame_].get(),
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
};
