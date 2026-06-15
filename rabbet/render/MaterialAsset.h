#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/render/ShaderUniform.h"

namespace rb {

struct ShaderAsset;
struct TextureAsset;

// A uniform value a material drives on its shader. The vec4 covers float/vec2/vec3/vec4 (in
// xyzw); `integer`/`boolean` hold the scalar kinds. Which field is live is `type`.
struct MaterialUniform {
    std::string name;
    UniformType type = UniformType::Unknown;
    glm::vec4 vec{0.0f};
    int integer = 0;
    bool boolean = false;
};

// A texture a material binds to a sampler uniform. `texture` is the stable id stored in the
// material file; `handle` is its runtime resolution, populated by MaterialAssetResolveSystem.
struct MaterialTexture {
    std::string name;
    Uuid texture;
    AssetHandle<TextureAsset> handle;
};

// A material owned by the AssetManager and referenced by uuid. It binds a shader (by uuid) and
// applies its `uniforms` + `textures` on top of the per-surface values the render system sets,
// so a material with no overrides is simply "draw this geometry with this shader". `revision`
// bumps on a file hot-reload; `sourceTimestamp` is the mtime the current data was read at.
struct MaterialAsset {
    Uuid shader;
    AssetHandle<ShaderAsset> shaderHandle;
    std::vector<MaterialUniform> uniforms;
    std::vector<MaterialTexture> textures;
    std::filesystem::path path;
    std::int64_t sourceTimestamp = 0;
    std::uint32_t revision = 0;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<MaterialAsset>() noexcept {
    return AssetType::Material;
}

} // namespace rb
