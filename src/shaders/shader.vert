#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;

// These positions are in clip coordinates [-1, 1]
vec2 positions[3] = vec2[](
    vec2(0, -1),
    vec2(1, 1),
    vec2(-1, 1)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

// gl_VertexIndex is the current vertex being read by the renderer!
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}