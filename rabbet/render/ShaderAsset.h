#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rabbet/assets/AssetType.h"
#include "rabbet/render/ShaderUniform.h"

namespace rb {

// A GLSL shader program owned by the AssetManager and referenced by uuid. The two stages are
// kept as source; the RenderSystem compiles them on demand and writes the reflected
// material-editable `uniforms` back here. `revision` bumps each time the source is hot-reloaded
// from disk so the program cache recompiles; `sourceTimestamp` is the mtime the text was read
// at. Built-in shaders (no `path`) are registered directly and never hot-reload.
struct ShaderAsset {
    std::string vertexSource;
    std::string fragmentSource;
    std::filesystem::path path;
    std::int64_t sourceTimestamp = 0;
    std::uint32_t revision = 0;
    std::vector<ShaderUniform> uniforms; // reflected after the first compile
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<ShaderAsset>() noexcept {
    return AssetType::Shader;
}

} // namespace rb
