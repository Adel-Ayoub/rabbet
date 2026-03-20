#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct ModelAsset;

// Draws a model asset. `model` is the stable id stored in scene files; `handle`
// is the runtime resolution of that id, filled in by the render system.
struct ModelRenderer {
    Uuid model;
    AssetHandle<ModelAsset> handle;
};

} // namespace rb
