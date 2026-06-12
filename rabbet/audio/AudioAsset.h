#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "rabbet/assets/AssetType.h"

namespace rb {

// An audio clip referenced by uuid and owned by the AssetManager. For formats miniaudio reads
// from disk itself (wav/mp3/flac) only the source path is kept and the bytes are decoded by the
// AudioSystem on demand. Ogg Vorbis has no native miniaudio decoder, so it is decoded to
// interleaved 16-bit PCM at import (via stb_vorbis) and played from memory; `samples` is empty
// for the file-backed formats.
struct AudioAsset {
    std::filesystem::path path;
    std::vector<std::int16_t> samples;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<AudioAsset>() noexcept {
    return AssetType::Audio;
}

} // namespace rb
