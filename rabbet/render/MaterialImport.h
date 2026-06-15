#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "rabbet/assets/AssetHandle.h"

namespace rb {

class AssetManager;
struct MaterialAsset;

// Serialises a material to its .material.json form: the shader uuid, the uniform overrides
// (name + type + value), and the texture bindings (name + uuid). Runtime handles are not
// written. `fromJson` is lenient — missing or malformed fields are skipped, not thrown — so a
// partly-broken file still loads. These are pure (no AssetManager), the unit-testable core.
[[nodiscard]] nlohmann::json materialToJson(const MaterialAsset& material);
void materialFromJson(const nlohmann::json& doc, MaterialAsset& material);

// Loads (or returns the cached) MaterialAsset for a .material.json file, recording its mtime.
// A missing or unparseable file yields an invalid handle. Shader/texture references are
// resolved later by MaterialAssetResolveSystem, not here.
[[nodiscard]] AssetHandle<MaterialAsset> loadMaterialAsset(AssetManager& assets,
                                                           const std::filesystem::path& path);

// Writes a material back to its file (so inspector edits can persist). Returns false on I/O
// failure or when the asset has no path.
bool saveMaterialAsset(AssetManager& assets, AssetHandle<MaterialAsset> handle);

// Re-reads the file if it changed on disk since it was last read, clearing resolved handles and
// bumping the revision so the resolve system re-links and the render cache refreshes. Returns
// true when it reloaded; a missing or unparseable file is a no-op.
bool reloadMaterialIfChanged(AssetManager& assets, AssetHandle<MaterialAsset> handle);

} // namespace rb
