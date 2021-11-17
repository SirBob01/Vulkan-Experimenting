#define TINYOBJLOADER_IMPLEMENTATION
#include "model.h"

Model::Model(const std::string obj_filename) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning, error;

    bool result = tinyobj::LoadObj(
        &attrib, 
        &shapes, 
        &materials, 
        &warning, 
        &error,
        obj_filename.c_str()
    );
    if(!result) {
        throw std::runtime_error("Could not load obj file: " + warning + error);
    }

    std::unordered_map<Vertex, uint32_t> unique_vertices;
    for(const auto &shape : shapes) {
        for(const auto &index : shape.mesh.indices) {
            Vertex vert;
            vert.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vert.color = {
                1.0, 1.0, 1.0, 1.0
            };
            vert.tex_coord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            if(unique_vertices.count(vert) == 0) {
                unique_vertices[vert] = vertices.size();
                vertices.push_back(vert);
            }
            indices.push_back(unique_vertices[vert]);
        }
    }
}