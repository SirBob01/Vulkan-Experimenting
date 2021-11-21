#include "vertex.h"

bool Vertex::operator==(const Vertex &other) const {
    return position == other.position && 
           color == other.color && 
           tex_coord == other.tex_coord;
}

vk::VertexInputBindingDescription Vertex::get_binding_description() {
    vk::VertexInputBindingDescription desc(
        0,              // Index in array of bindings
        sizeof(Vertex)  // Stride (memory buffer traversal)
    );
    return desc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::get_attribute_descriptions() {
    std::vector<vk::VertexInputAttributeDescription> descriptions;
    descriptions.push_back({
        0, 0, 
        vk::Format::eR32G32B32Sfloat, 
        offsetof(Vertex, position)
    });
    descriptions.push_back({
        1, 0, 
        vk::Format::eR32G32B32A32Sfloat, 
        offsetof(Vertex, color)
    });
    descriptions.push_back({
        2, 0,
        vk::Format::eR32G32Sfloat,
        offsetof(Vertex, tex_coord)
    });
    return descriptions;
}

size_t std::hash<Vertex>::operator()(Vertex const &vertex) const {
    size_t hash1 = std::hash<glm::vec3>()(vertex.position);
    size_t hash2 = std::hash<glm::vec4>()(vertex.color);
    size_t hash3 = std::hash<glm::vec2>()(vertex.tex_coord);
    return ((hash1 ^ (hash2 << 1)) >> 1) ^ (hash3 << 1);
}