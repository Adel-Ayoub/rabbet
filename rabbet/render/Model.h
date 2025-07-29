#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "rabbet/render/MeshData.h"

namespace rb {

struct ModelMaterial {
    std::string baseColorTexture;
    glm::vec3 baseColor{1.0f};
};

struct ModelMesh {
    MeshData data;
    int materialIndex = -1;
};

struct Model {
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
};

} // namespace rb
