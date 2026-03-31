#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct ModelAsset;

// Draws a model asset. `model` is the stable id stored in scene files; `handle` is
// its runtime resolution, populated by AssetResolveSystem (the render system also
// resolves it as a fallback). Do not also place a gl::Mesh on the same entity, or
// the geometry will be drawn twice.
struct ModelRenderer {
    Uuid model;
    AssetHandle<ModelAsset> handle;
};

} // namespace rb
