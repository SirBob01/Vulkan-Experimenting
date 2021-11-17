#ifndef VERTEX_H_
#define VERTEX_H_

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 tex_coord;

    bool operator==(const Vertex &other) const;
    static vk::VertexInputBindingDescription get_binding_desc();
    static std::array<vk::VertexInputAttributeDescription, 3> get_attribute_desc();
};

// Custom hash function for vertices
template <>
struct std::hash<Vertex> {
    std::size_t operator()(Vertex const &vertex) const;
};

#endif