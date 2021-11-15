#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(binding = 1) uniform sampler2D textureSamplers[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in int textureIndex;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texture = texture(textureSamplers[textureIndex], fragTexCoord);
    outColor = fragColor.rgba * texture.rgba;
}