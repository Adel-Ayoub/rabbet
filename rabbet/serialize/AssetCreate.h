#pragma once

#include <filesystem>
#include <string>

namespace rb {

class ComponentRegistry;

// Filesystem-safe base name for a new asset file: junk characters become '_', leading and
// trailing dots/spaces are trimmed, an empty result takes `fallback`, and the length is capped
// so the final "<base>_NNN.<suffix>" always stays far inside NAME_MAX.
[[nodiscard]] std::string sanitizeAssetName(std::string name, const std::string& fallback);

// Builds a non-colliding "<dir>/<sanitized><suffix>" path, appending _1, _2, ... when a file
// with that name already exists. An exhausted namespace (1000 same-named siblings) returns an
// empty path rather than renaming over the last candidate.
[[nodiscard]] std::filesystem::path assetFilePath(const std::filesystem::path& dir,
                                                  const std::string& name,
                                                  const std::string& suffix,
                                                  const std::string& fallback);

// Each writes a minimal valid template atomically and returns the final path, or an empty path
// on failure. The caller re-scans the AssetDatabase to catalogue the new file (the scan creates
// its .import sidecar and uuid).
[[nodiscard]] std::filesystem::path createScriptAsset(const std::filesystem::path& dir,
                                                      const std::string& name);
[[nodiscard]] std::filesystem::path createMaterialAsset(const std::filesystem::path& dir,
                                                        const std::string& name);
[[nodiscard]] std::filesystem::path createSceneAsset(const std::filesystem::path& dir,
                                                     const std::string& name,
                                                     const ComponentRegistry& registry);

} // namespace rb
