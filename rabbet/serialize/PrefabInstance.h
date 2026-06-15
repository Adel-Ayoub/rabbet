#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct PrefabAsset;

// Marks an entity as an instance of a prefab. `prefab` is the stable source id stored in scene
// files; `handle` is its runtime resolution, populated by PrefabAssetResolveSystem. The link is
// metadata: the entity's components live in the scene as usual, so local edits are overrides;
// reverting re-applies the prefab's component data. A plain entity (no link) is never touched.
struct PrefabInstance {
    Uuid prefab;
    AssetHandle<PrefabAsset> handle;
};

} // namespace rb
