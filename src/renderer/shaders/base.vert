#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 transform;
} ubo;
layout(push_constant) uniform ObjectData {
    int textureIndex;
    int xOffset;
    int yOffset;
    int width;
    int height;
} PushConstant;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out int textureIndex;

// gl_VertexIndex is the current vertex being read by the renderer!
void main() {
    gl_Position = ubo.transform * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    textureIndex = PushConstant.textureIndex;
}