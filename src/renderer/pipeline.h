#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <vulkan/vulkan.hpp>

#include <vector>
#include <fstream>
#include <iostream>

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

    void create_shader_stage(std::string filename, 
                             vk::ShaderStageFlagBits stage);

    void create_vertex_input_state();

    void create_assembly_state(vk::PrimitiveTopology primitive_topology);

    void create_viewport_state(vk::Extent2D &extent);

    void create_rasterization_state(vk::PolygonMode polygon_mode);

    void create_multisampler_state(vk::SampleCountFlagBits msaa_samples);

    void create_blender_state();
    
    void create_depth_stencil_state();

    void create_dynamic_state();

    void create_layout(vk::DescriptorSetLayout &set_layout, 
                       std::size_t push_constants_size);

    void assemble_stages(vk::RenderPass &render_pass);
public:
    Pipeline(vk::Device &logical,
             vk::Extent2D &image_extent,
             vk::DescriptorSetLayout &set_layout,
             vk::RenderPass &render_pass,
             std::vector<std::string> &vertex_shaders,
             std::vector<std::string> &fragment_shaders,
             vk::PrimitiveTopology primitive_topology,
             vk::PolygonMode polygon_mode,
             vk::SampleCountFlagBits msaa_samples,
             std::size_t push_constants_size);

    vk::Pipeline &get_handle();

    vk::PipelineLayout &get_layout();
};

#endif