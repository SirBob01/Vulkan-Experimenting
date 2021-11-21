#ifndef VERTEX_H_
#define VERTEX_H_

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <vector>

struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 tex_coord;

    bool operator==(const Vertex &other) const;
    static vk::VertexInputBindingDescription get_binding_description();
    static std::vector<vk::VertexInputAttributeDescription> get_attribute_descriptions();
};

// Custom hash function for vertices
template <>
struct std::hash<Vertex> {
    std::size_t operator()(Vertex const &vertex) const;
};

#endif