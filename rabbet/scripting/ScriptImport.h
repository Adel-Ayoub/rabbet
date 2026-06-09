#pragma once

#include <filesystem>

#include "rabbet/assets/AssetHandle.h"

namespace rb {

class AssetManager;
struct ScriptAsset;

// Loads (or returns the cached) ScriptAsset for a .lua file, reading its text and mtime.
[[nodiscard]] AssetHandle<ScriptAsset> loadScriptAsset(AssetManager& assets,
                                                       const std::filesystem::path& path);

// Re-reads the .lua text if the file changed on disk since it was last read, bumping the
// asset's revision and returning true when it reloaded. This is the hot-reload poll
// (SCRIPT-05); a missing file or unresolved handle is a no-op.
bool reloadScriptIfChanged(AssetManager& assets, AssetHandle<ScriptAsset> handle);

} // namespace rb
