#include "rabbet/render/ShaderImport.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/ShaderAsset.h"

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

bool hasNonSpace(std::string_view text) {
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<ShaderSourceParts> parseShaderSource(std::string_view text) {
    constexpr std::string_view kVertexMarker = "#VERTEX";
    constexpr std::string_view kFragmentMarker = "#FRAGMENT";
    const std::size_t vpos = text.find(kVertexMarker);
    const std::size_t fpos = text.find(kFragmentMarker);
    if (vpos == std::string_view::npos || fpos == std::string_view::npos || fpos <= vpos) {
        return std::nullopt;
    }
    // Start each stage after its marker's line so the `#VERTEX` / `#FRAGMENT` token itself is
    // never fed to the GLSL compiler.
    const auto lineEnd = [&](std::size_t markerPos, std::size_t markerLen) {
        const std::size_t nl = text.find('\n', markerPos + markerLen);
        return nl == std::string_view::npos ? text.size() : nl + 1;
    };
    const std::size_t vstart = lineEnd(vpos, kVertexMarker.size());
    const std::string_view vertex = text.substr(vstart, fpos - vstart);
    const std::size_t fstart = lineEnd(fpos, kFragmentMarker.size());
    const std::string_view fragment = text.substr(fstart);
    if (!hasNonSpace(vertex) || !hasNonSpace(fragment)) {
        return std::nullopt;
    }
    return ShaderSourceParts{std::string(vertex), std::string(fragment)};
}

AssetHandle<ShaderAsset> loadShaderAsset(AssetManager& assets, const std::filesystem::path& path) {
    return assets.load<ShaderAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<ShaderAsset> {
            const std::optional<std::string> text = readFile(p);
            if (!text.has_value()) {
                return std::nullopt;
            }
            const std::optional<ShaderSourceParts> parts = parseShaderSource(*text);
            if (!parts.has_value()) {
                return std::nullopt;
            }
            ShaderAsset asset;
            asset.vertexSource = parts->vertex;
            asset.fragmentSource = parts->fragment;
            asset.path = p;
            asset.sourceTimestamp = mtimeOf(p);
            return asset;
        });
}

bool reloadShaderIfChanged(AssetManager& assets, AssetHandle<ShaderAsset> handle) {
    ShaderAsset* asset = assets.get<ShaderAsset>(handle);
    if (asset == nullptr || asset->path.empty()) {
        return false;
    }
    const std::int64_t current = mtimeOf(asset->path);
    if (current == 0 || current == asset->sourceTimestamp) {
        return false;
    }
    const std::optional<std::string> text = readFile(asset->path);
    if (!text.has_value()) {
        return false;
    }
    const std::optional<ShaderSourceParts> parts = parseShaderSource(*text);
    if (!parts.has_value()) {
        return false;
    }
    asset->vertexSource = parts->vertex;
    asset->fragmentSource = parts->fragment;
    asset->sourceTimestamp = current;
    ++asset->revision;
    return true;
}

} // namespace rb
