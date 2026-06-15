#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct MaterialAsset;

// Shades an entity's geometry with a material asset. `material` is the stable id stored in
// scene files; `handle` is its runtime resolution, populated by MaterialAssetResolveSystem.
// The render system draws the entity's geometry (mesh / primitive / model submeshes) with the
// material's shader, applying its uniform and texture overrides on top of the per-surface
// values. An unresolved material leaves the entity drawn by the built-in shader instead.
struct MaterialComponent {
    Uuid material;
    AssetHandle<MaterialAsset> handle;
};

} // namespace rb
