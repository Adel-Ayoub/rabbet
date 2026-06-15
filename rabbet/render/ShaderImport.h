#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "rabbet/assets/AssetHandle.h"

namespace rb {

class AssetManager;
struct ShaderAsset;

// The two GLSL stages parsed out of a single .shader/.glsl file.
struct ShaderSourceParts {
    std::string vertex;
    std::string fragment;
};

// Splits a combined shader file into its stages. The file marks them with `#VERTEX` and
// `#FRAGMENT` lines (the vertex stage between the two markers, the fragment stage after).
// Returns nullopt if either marker is missing, out of order, or its section is empty. Pure:
// no I/O, so it is the unit-testable half of the importer.
[[nodiscard]] std::optional<ShaderSourceParts> parseShaderSource(std::string_view text);

// Loads (or returns the cached) ShaderAsset for a .shader/.glsl file, parsing its stages and
// recording the mtime. A missing file or one without both stages yields an invalid handle.
[[nodiscard]] AssetHandle<ShaderAsset> loadShaderAsset(AssetManager& assets,
                                                       const std::filesystem::path& path);

// Re-reads and re-parses the file if it changed on disk since it was last read, bumping the
// asset's revision and returning true when it reloaded. This is the hot-reload poll; a missing
// file, unparseable contents, or an unresolved handle is a no-op that leaves the old source.
bool reloadShaderIfChanged(AssetManager& assets, AssetHandle<ShaderAsset> handle);

} // namespace rb
