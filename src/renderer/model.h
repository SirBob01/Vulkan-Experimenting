#ifndef MODEL_H_
#define MODEL_H_

#include <vector>
#include <unordered_map>

#include "assets/tiny_obj_loader.h"
#include "vertex.h"


struct Model {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Model() {};
    Model(const std::string obj_filename);
};

#endif