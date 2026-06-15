#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "rabbet/assets/AssetType.h"

namespace rb {

// A reusable single-entity template owned by the AssetManager and referenced by uuid. `components`
// is the serialized component map (component name -> data), the same shape SceneSerializer writes
// per entity, so instantiating reuses the ComponentRegistry load hooks. `path` is the source file
// it was read from. Rabbet entities are flat (no hierarchy), so a prefab captures one entity;
// nested/multi-entity prefabs wait on an entity hierarchy.
struct PrefabAsset {
    nlohmann::json components = nlohmann::json::object();
    std::filesystem::path path;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<PrefabAsset>() noexcept {
    return AssetType::Prefab;
}

} // namespace rb
