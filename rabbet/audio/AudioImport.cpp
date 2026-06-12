#include "rabbet/audio/AudioImport.h"

#include <optional>
#include <system_error>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/audio/AudioAsset.h"

namespace rb {

AssetHandle<AudioAsset> loadAudioAsset(AssetManager& assets, const std::filesystem::path& path) {
    return assets.load<AudioAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<AudioAsset> {
            std::error_code ec;
            if (!std::filesystem::exists(p, ec) || ec) {
                return std::nullopt;
            }
            AudioAsset asset;
            asset.path = p;
            return asset;
        });
}

} // namespace rb
