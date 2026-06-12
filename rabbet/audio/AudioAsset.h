#pragma once

#include <filesystem>

#include "rabbet/assets/AssetType.h"

namespace rb {

// An audio clip referenced by uuid and owned by the AssetManager. Only the source path is
// kept; the samples are decoded on demand by the AudioSystem (miniaudio reads wav/mp3/flac
// directly), so importing an emitter's clip never pulls audio bytes into memory here.
struct AudioAsset {
    std::filesystem::path path;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<AudioAsset>() noexcept {
    return AssetType::Audio;
}

} // namespace rb
