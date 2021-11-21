#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <vulkan/vulkan.hpp>

#include <vector>
#include <fstream>

#include "vertex.h"

class Pipeline {
    vk::Device logical_;
    vk::UniquePipelineLayout layout_;
    vk::UniquePipeline pipeline_;
    
    std::vector<vk::UniqueShaderModule> shader_modules_;

    vk::VertexInputBindingDescription binding_description_;
    std::vector<vk::VertexInputAttributeDescription> attribute_descriptions_;
    
    vk::Viewport viewport_;
    vk::Rect2D scissor_;

    vk::PipelineColorBlendAttachmentState blender_attachment_;

    // Pipeline states
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stage_infos_;
    vk::PipelineVertexInputStateCreateInfo vertex_input_state_info_;
    vk::PipelineInputAssemblyStateCreateInfo assembly_state_info_;
    vk::PipelineViewportStateCreateInfo viewport_state_info_;
    vk::PipelineRasterizationStateCreateInfo rasterization_state_info_;
    vk::PipelineMultisampleStateCreateInfo multisampler_state_info_;
    vk::PipelineColorBlendStateCreateInfo blender_state_info_;
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_info_;
    vk::PipelineDynamicStateCreateInfo dynamic_state_info_;

    // Load the compiled shader code and create each shader stage
    void create_shader_stage(std::string filename, 
                             vk::ShaderStageFlagBits stage);

    // Describe the vertex input data
    void create_vertex_input_state();

    // Describe the input assembly mode
    void create_assembly_state(vk::PrimitiveTopology primitive_topology);

    // Describe the viewport
    void create_viewport_state(vk::Extent2D &extent);

    // Describe the rasterization process
    void create_rasterization_state(vk::PolygonMode polygon_mode);

    // Describe the multisampling process
    void create_multisampler_state(vk::SampleCountFlagBits msaa_samples);

    // Describe the color blender
    // TODO: Can be customized to allow different blend modes
    void create_blender_state();
    
    // Describe the depth stencil
    void create_depth_stencil_state();

    // Specify all dynamic states that can be manipulated
    // from the command buffer
    void create_dynamic_state();

    // Create the pipeline layout
    void create_layout(vk::DescriptorSetLayout &set_layout, 
                       size_t push_constants_size);

    // Assemble the pipeline
    void assemble_stages(vk::RenderPass &render_pass);

public:
    Pipeline(vk::Device &logical,
             vk::Extent2D &image_extent,
             vk::DescriptorSetLayout &set_layout,
             vk::RenderPass &render_pass,
             std::string vertex_shader,
             std::string fragment_shader,
             vk::PrimitiveTopology primitive_topology,
             vk::PolygonMode polygon_mode,
             vk::SampleCountFlagBits msaa_samples,
             size_t push_constants_size);

    // Get the handle to the pipeline
    vk::Pipeline &get_handle();

    // Get the handle to the pipeline's layout
    vk::PipelineLayout &get_layout();
};

#endif