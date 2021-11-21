#include "pipeline.h"

Pipeline::Pipeline(vk::Device &logical,
                   vk::Extent2D &image_extent,
                   vk::DescriptorSetLayout &set_layout,
                   vk::RenderPass &render_pass,
                   std::string vertex_shader,
                   std::string fragment_shader,
                   vk::PrimitiveTopology primitive_topology,
                   vk::PolygonMode polygon_mode,
                   vk::SampleCountFlagBits msaa_samples,
                   std::size_t push_constants_size) {
    logical_ = logical;

    create_shader_stage(
        vertex_shader, 
        vk::ShaderStageFlagBits::eVertex
    );
    create_shader_stage(
        fragment_shader, 
        vk::ShaderStageFlagBits::eFragment
    );
    
    create_vertex_input_state();
    create_assembly_state(primitive_topology);
    create_viewport_state(image_extent);
    create_rasterization_state(polygon_mode);
    create_multisampler_state(msaa_samples);
    create_blender_state();
    create_depth_stencil_state();
    create_dynamic_state();
    create_layout(set_layout, push_constants_size);
    
    assemble_stages(render_pass);

    // Save memory by deleting used shader modules
    shader_modules_.clear();
}

void Pipeline::create_shader_stage(std::string filename, 
                                   vk::ShaderStageFlagBits stage) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if(!file.is_open()) {
        throw std::runtime_error("Failed to load shader: " + filename);
    }

    size_t size = file.tellg();
    std::vector<char> bytes(size);
    
    file.seekg(0);
    file.read(&bytes[0], size);
    file.close();

    // Create the shader module from the file
    vk::ShaderModuleCreateInfo shader_info;
    shader_info.codeSize = bytes.size();
    shader_info.pCode = reinterpret_cast<uint32_t *>(&bytes[0]);

    shader_modules_.push_back(
        logical_.createShaderModuleUnique(
            shader_info
        )
    );

    // Create the pipeline stage
    vk::PipelineShaderStageCreateInfo stage_info;
    stage_info.stage = stage;
    stage_info.module = shader_modules_.back().get();
    stage_info.pName = "main"; // Entry function

    shader_stage_infos_.push_back(stage_info);
}

void Pipeline::create_vertex_input_state() {
    binding_description_ = Vertex::get_binding_description();
    attribute_descriptions_ = Vertex::get_attribute_descriptions();
    
    vertex_input_state_info_.vertexBindingDescriptionCount = 1;
    vertex_input_state_info_.pVertexBindingDescriptions = &binding_description_;
    vertex_input_state_info_.vertexAttributeDescriptionCount = attribute_descriptions_.size();
    vertex_input_state_info_.pVertexAttributeDescriptions = &attribute_descriptions_[0];
}

void Pipeline::create_assembly_state(vk::PrimitiveTopology primitive_topology) {
    assembly_state_info_.topology = primitive_topology;
    assembly_state_info_.primitiveRestartEnable = false;
}

void Pipeline::create_viewport_state(vk::Extent2D &extent) {
    // Origin
    viewport_.x = 0.0f;
    viewport_.y = 0.0f;

    // Dimensions
    viewport_.width = static_cast<float>(extent.width);
    viewport_.height = static_cast<float>(extent.height);

    // Render depth range
    viewport_.minDepth = 0.0f;
    viewport_.maxDepth = 1.0f;

    // Subsection of the viewport to be used
    scissor_.offset.x = 0;
    scissor_.offset.y = 0;
    scissor_.extent = extent;

    viewport_state_info_.viewportCount = 1;
    viewport_state_info_.pViewports = &viewport_;
    viewport_state_info_.scissorCount = 1;
    viewport_state_info_.pScissors = &scissor_;
}

void Pipeline::create_rasterization_state(vk::PolygonMode polygon_mode) {
    rasterization_state_info_.depthClampEnable = false;
    rasterization_state_info_.rasterizerDiscardEnable = false;

    // How to draw polygon
    // * eFill - Standard
    // * eLine - Wireframe
    // * ePoint - Point cloud
    rasterization_state_info_.polygonMode = polygon_mode;
    rasterization_state_info_.lineWidth = 1.0;

    // Backface culling?
    rasterization_state_info_.cullMode = vk::CullModeFlagBits::eBack;
    rasterization_state_info_.frontFace = vk::FrontFace::eCounterClockwise;
    
    // Manipulate the depth values?
    // TODO: Could be useful for shadow mapping
    rasterization_state_info_.depthBiasEnable = false;
    rasterization_state_info_.depthBiasConstantFactor = 0.0f;
    rasterization_state_info_.depthBiasClamp = 0.0f;
    rasterization_state_info_.depthBiasSlopeFactor = 0.0f;
}

void Pipeline::create_multisampler_state(vk::SampleCountFlagBits msaa_samples) {
    multisampler_state_info_.rasterizationSamples = msaa_samples;
    multisampler_state_info_.sampleShadingEnable = true;
    
    // Multisampling rate for anti-aliasing the texture
    multisampler_state_info_.minSampleShading = 0.5f; 
}

void Pipeline::create_blender_state() {
    // TODO: Allow custom blend function? Add, subtract, multiply, etc.
    blender_attachment_.blendEnable = true;
    blender_attachment_.colorWriteMask = 
        vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;

    // RGB blending operation
    blender_attachment_.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blender_attachment_.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;

    // Alpha blending operation
    blender_attachment_.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blender_attachment_.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    blender_attachment_.alphaBlendOp = vk::BlendOp::eAdd;

    // Create the blender state
    blender_state_info_.logicOpEnable = false;
    blender_state_info_.logicOp = vk::LogicOp::eNoOp;
    blender_state_info_.attachmentCount = 1;
    blender_state_info_.pAttachments = &blender_attachment_;
}

void Pipeline::create_depth_stencil_state() {
    depth_stencil_state_info_.depthTestEnable = true;
    depth_stencil_state_info_.depthWriteEnable = true;
    depth_stencil_state_info_.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil_state_info_.depthBoundsTestEnable = false;
    depth_stencil_state_info_.stencilTestEnable = false;
}

void Pipeline::create_dynamic_state() {
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eLineWidth,     // Change width of line drawing
        vk::DynamicState::eBlendConstants // Change blending function
    };
    dynamic_state_info_.dynamicStateCount = dynamic_states.size();
    dynamic_state_info_.pDynamicStates = &dynamic_states[0];
}

void Pipeline::create_layout(vk::DescriptorSetLayout &set_layout,
                             std::size_t push_constants_size) {
    vk::PushConstantRange push_constant_range;
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eVertex;
    push_constant_range.offset = 0;
    push_constant_range.size = push_constants_size;

    vk::PipelineLayoutCreateInfo layout_info;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;

    layout_ = logical_.createPipelineLayoutUnique(layout_info);
}

void Pipeline::assemble_stages(vk::RenderPass &render_pass) {
    vk::GraphicsPipelineCreateInfo pipeline_info;
    
    // Shader stages
    pipeline_info.stageCount = shader_stage_infos_.size();
    pipeline_info.pStages = &shader_stage_infos_[0];

    // Assembly stages
    pipeline_info.pVertexInputState = &vertex_input_state_info_;
    pipeline_info.pInputAssemblyState = &assembly_state_info_;

    // Fixed function stages
    pipeline_info.pViewportState = &viewport_state_info_;
    pipeline_info.pRasterizationState = &rasterization_state_info_;
    pipeline_info.pMultisampleState = &multisampler_state_info_;
    pipeline_info.pDepthStencilState = &depth_stencil_state_info_;
    pipeline_info.pColorBlendState = &blender_state_info_;
    pipeline_info.pDynamicState = &dynamic_state_info_;
    
    // Layout and render pass
    pipeline_info.layout = layout_.get();
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0; // Subpass index

    // Derived pipeline?
    pipeline_info.basePipelineHandle = nullptr;
    pipeline_info.basePipelineIndex = 0;

    pipeline_ = logical_.createGraphicsPipelineUnique(
        nullptr, // TODO: Pipeline cache 
        pipeline_info
    ).value;
}

vk::Pipeline &Pipeline::get_handle() {
    return pipeline_.get();
}

vk::PipelineLayout &Pipeline::get_layout() {
    return layout_.get();
}