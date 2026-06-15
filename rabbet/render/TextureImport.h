#pragma once

#include <filesystem>

#include "rabbet/assets/AssetHandle.h"

namespace rb {

class AssetManager;
struct TextureAsset;

// Loads (or returns the cached) TextureAsset for an image file, uploading it as an sRGB colour
// texture. Requires a live GL context. A missing or unreadable image yields an invalid handle.
// This is the standalone texture loader material texture bindings resolve through (models still
// upload their own albedo at import).
[[nodiscard]] AssetHandle<TextureAsset> loadTextureAsset(AssetManager& assets,
                                                         const std::filesystem::path& path);

} // namespace rb
