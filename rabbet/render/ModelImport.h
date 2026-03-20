#pragma once

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/render/ModelAsset.h"

namespace rb {

class AssetManager;

struct ModelImportOptions {
    float metallic = 0.0f;
    float roughness = 0.8f;
};

// Imports a model file (assimp) into GPU meshes and shared albedo textures.
// Requires a live GL context. Textures are registered in `assets`.
[[nodiscard]] std::optional<ModelAsset> importModel(AssetManager& assets,
                                                    const std::filesystem::path& path,
                                                    const ModelImportOptions& options = {});

// Imports the model and registers it under the stable uuid from its sidecar,
// returning a cached handle on repeat calls.
[[nodiscard]] AssetHandle<ModelAsset> loadModelAsset(AssetManager& assets,
                                                     const std::filesystem::path& path,
                                                     const ModelImportOptions& options = {});

} // namespace rb
