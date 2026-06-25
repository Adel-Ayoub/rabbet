#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/render/gl/Mesh.h"

namespace rb {

struct TextureAsset;

// A drawable model owned by the AssetManager. Meshes are model-private (uploaded
// once on import); albedo textures are shared assets referenced by handle.
struct ModelAsset {
    struct Submesh {
        gl::Mesh mesh;
        glm::vec3 baseColor{1.0f};
        glm::vec3 emissive{0.0f};
        float metallic = 0.0f;
        float roughness = 0.8f;
        float ao = 1.0f;
        AssetHandle<TextureAsset> albedo;
    };

    std::vector<Submesh> submeshes;

    // A bounding sphere over all submesh vertices, in the model's local space, so a previewer
    // (and, later, frustum culling) can frame the model without re-reading its geometry. Defaults
    // to the unit sphere for a model with no vertices.
    glm::vec3 boundsCenter{0.0f};
    float boundsRadius = 1.0f;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<ModelAsset>() noexcept {
    return AssetType::Model;
}

} // namespace rb
