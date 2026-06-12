#pragma once

#include <filesystem>

#include "rabbet/assets/AssetHandle.h"

namespace rb {

class AssetManager;
struct AudioAsset;

// Loads (or returns the cached) AudioAsset for an audio file, recording its path. The samples
// are decoded on demand by the AudioSystem, not here; a missing file yields an invalid handle.
[[nodiscard]] AssetHandle<AudioAsset> loadAudioAsset(AssetManager& assets,
                                                     const std::filesystem::path& path);

} // namespace rb
