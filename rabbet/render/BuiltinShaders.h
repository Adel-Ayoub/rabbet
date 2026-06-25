#pragma once

#include <string>

#include "rabbet/core/Uuid.h"

namespace rb {

class AssetManager;

// Stable uuids for the engine's built-in render assets. They are registered directly (not from
// files) so the data-driven path always has a shader/material to fall back on, and scenes can
// reference the defaults by a fixed id. Random file-asset uuids never collide with these.
namespace builtin {
inline constexpr Uuid kPbrShader{0xB1117D00'00000001ULL, 0x0000000000000050ULL};
inline constexpr Uuid kPhongShader{0xB1117D00'00000001ULL, 0x0000000000000051ULL};
inline constexpr Uuid kDefaultMaterial{0xB1117D00'00000001ULL, 0x0000000000000052ULL};
} // namespace builtin

// The canonical GLSL for the built-in lit shaders. These are the single source of truth: the
// RenderSystem compiles them for its fallback programs, and registerDefaultRenderAssets wraps
// the same strings into ShaderAssets, so the data-driven path renders identically.
[[nodiscard]] const std::string& builtinLitVertexSource();
[[nodiscard]] const std::string& builtinPbrFragmentSource();
[[nodiscard]] const std::string& builtinPhongFragmentSource();

// The built-in terrain shader: a lit, splat-blended heightfield shader sharing the PBR brdf + light
// helpers. A dedicated render pass compiles it (it is not a material-assignable ShaderAsset).
[[nodiscard]] const std::string& builtinTerrainVertexSource();
[[nodiscard]] const std::string& builtinTerrainFragmentSource();

// Registers the built-in PBR and Phong shaders, plus a default PBR material (no overrides) that
// binds the PBR shader, into the AssetManager under the fixed uuids above. Idempotent: a second
// call is a no-op. Lets a MaterialComponent reference the defaults without any asset file.
void registerDefaultRenderAssets(AssetManager& assets);

} // namespace rb
