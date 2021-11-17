#include "vertex.h"


bool Vertex::operator==(const Vertex &other) const {
    return pos == other.pos && 
            color == other.color && 
            tex_coord == other.tex_coord;
}

vk::VertexInputBindingDescription Vertex::get_binding_desc() {
    vk::VertexInputBindingDescription desc(
        0,              // Index in array of bindings
        sizeof(Vertex)  // Stride (memory buffer traversal)
    );
    return desc;
}

std::array<vk::VertexInputAttributeDescription, 3> Vertex::get_attribute_desc() {
    std::array<vk::VertexInputAttributeDescription, 3> desc;
    desc[0] = {
        0, 0, 
        vk::Format::eR32G32B32Sfloat, 
        offsetof(Vertex, pos)   // Memory offset of position member
    };
    desc[1] = {
        1, 0, 
        vk::Format::eR32G32B32A32Sfloat, 
        offsetof(Vertex, color) // Memory offset of color member
    };
    desc[2] = {
        2, 0,
        vk::Format::eR32G32Sfloat,
        offsetof(Vertex, tex_coord)
    };
    return desc;
}

std::size_t std::hash<Vertex>::operator()(Vertex const &vertex) const {
    std::size_t hash1 = std::hash<glm::vec3>()(vertex.pos);
    std::size_t hash2 = std::hash<glm::vec4>()(vertex.color);
    std::size_t hash3 = std::hash<glm::vec2>()(vertex.tex_coord);
    return ((hash1 ^ (hash2 << 1)) >> 1) ^ (hash3 << 1);
}