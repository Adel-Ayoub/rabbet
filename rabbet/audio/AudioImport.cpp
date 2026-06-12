#include "rabbet/audio/AudioImport.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/audio/AudioAsset.h"

// stb_vorbis is C; pull in only its declarations and give them C linkage so they match the
// C-compiled implementation in the third_party stb_vorbis library.
#define STB_VORBIS_HEADER_ONLY
extern "C" {
#include "stb_vorbis.c"
}

namespace rb {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// Decode a whole Ogg Vorbis file to interleaved 16-bit PCM. miniaudio has no native Vorbis
// decoder, so the clip is held in memory and played from a buffer rather than streamed.
bool decodeOgg(const std::filesystem::path& path, AudioAsset& asset) {
    int channels = 0;
    int sampleRate = 0;
    short* output = nullptr;
    const int frames =
        stb_vorbis_decode_filename(path.string().c_str(), &channels, &sampleRate, &output);
    if (frames < 0 || output == nullptr || channels <= 0) {
        std::free(output); // free(nullptr) is a no-op
        return false;
    }
    const std::size_t total =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels);
    asset.samples.assign(output, output + total);
    asset.channels = static_cast<std::uint32_t>(channels);
    asset.sampleRate = static_cast<std::uint32_t>(sampleRate);
    std::free(output);
    return true;
}

} // namespace

AssetHandle<AudioAsset> loadAudioAsset(AssetManager& assets, const std::filesystem::path& path) {
    return assets.load<AudioAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<AudioAsset> {
            std::error_code ec;
            if (!std::filesystem::exists(p, ec) || ec) {
                return std::nullopt;
            }
            AudioAsset asset;
            asset.path = p;
            if (lowerExtension(p) == ".ogg" && !decodeOgg(p, asset)) {
                return std::nullopt; // an .ogg that will not decode is a failed import
            }
            return asset;
        });
}

} // namespace rb
