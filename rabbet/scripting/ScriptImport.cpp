#include "rabbet/scripting/ScriptImport.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/scripting/ScriptAsset.h"

namespace rb {
namespace {

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::int64_t mtimeOf(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace

AssetHandle<ScriptAsset> loadScriptAsset(AssetManager& assets,
                                         const std::filesystem::path& path) {
    return assets.load<ScriptAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<ScriptAsset> {
            std::optional<std::string> text = readFile(p);
            if (!text.has_value()) {
                return std::nullopt;
            }
            ScriptAsset asset;
            asset.source = std::move(*text);
            asset.path = p;
            asset.sourceTimestamp = mtimeOf(p);
            return asset;
        });
}

bool reloadScriptIfChanged(AssetManager& assets, AssetHandle<ScriptAsset> handle) {
    ScriptAsset* asset = assets.get<ScriptAsset>(handle);
    if (asset == nullptr || asset->path.empty()) {
        return false;
    }
    const std::int64_t current = mtimeOf(asset->path);
    if (current == 0 || current == asset->sourceTimestamp) {
        return false;
    }
    std::optional<std::string> text = readFile(asset->path);
    if (!text.has_value()) {
        return false;
    }
    asset->source = std::move(*text);
    asset->sourceTimestamp = current;
    ++asset->revision;
    return true;
}

} // namespace rb
