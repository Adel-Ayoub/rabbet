#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/render/gl/Mesh.h"

namespace rb {

struct TextureAsset;

// A drawable model owned by the AssetManager. Meshes are model-private (uploaded
// once on import); albedo textures are shared assets referenced by handle.
struct ModelAsset {
    struct Submesh {
        gl::Mesh mesh;
        glm::vec3 baseColor{1.0f};
        float metallic = 0.0f;
        float roughness = 0.8f;
        float ao = 1.0f;
        AssetHandle<TextureAsset> albedo;
    };

    std::vector<Submesh> submeshes;
};

} // namespace rb
