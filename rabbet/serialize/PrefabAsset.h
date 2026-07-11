#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "rabbet/assets/AssetType.h"

namespace rb {

// A reusable entity-subtree template owned by the AssetManager and referenced by uuid.
// `entities` is an array of records {id, parent?, components}, each record holding the same
// component map SceneSerializer writes, so instantiating reuses the ComponentRegistry load
// hooks. `parent` refers to another record's position in the array; the root record carries
// none. Loading normalizes the older single-entity shape ({version, components}) into a
// one-record array, so old prefab files keep working unchanged. `path` is the source file it
// was read from.
struct PrefabAsset {
    nlohmann::json entities = nlohmann::json::array();
    std::filesystem::path path;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<PrefabAsset>() noexcept {
    return AssetType::Prefab;
}

} // namespace rb
