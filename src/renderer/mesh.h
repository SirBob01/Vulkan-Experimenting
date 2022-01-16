#ifndef MESH_H_
#define MESH_H_

#include <vector>
#include <unordered_map>

#include "assets/tiny_obj_loader.h"
#include "vertex.h"

// Stores the raw mesh data
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Mesh() {};
    Mesh(const std::string obj_filename);
};

#endif