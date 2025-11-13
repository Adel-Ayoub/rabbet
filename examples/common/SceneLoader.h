#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "rabbet/core/Runtime.h"
#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/scene/Transform.h"

namespace ex {

// Loads a model and spawns its geometry into the scene at `base`. Submeshes that
// share a base-colour texture are merged into one entity, so a 100-primitive model
// becomes a handful of draw calls instead of one entity per primitive.
void spawnModel(rb::Runtime& runtime, const std::filesystem::path& modelPath,
                const rb::Transform& base, float metallic = 0.0f, float roughness = 0.8f);

// Builds a cubemap from six face images in `dir` named by `faces` in the order
// +X, -X, +Y, -Y, +Z, -Z (right, left, top, bottom, front, back).
[[nodiscard]] rb::gl::Cubemap loadCubemap(const std::filesystem::path& dir,
                                          const std::array<std::string, 6>& faces);

} // namespace ex
