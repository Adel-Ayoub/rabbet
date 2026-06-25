#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct TextureAsset;

// Where a terrain's heights come from.
enum class TerrainHeightSource {
    Flat,      // a flat plane (heightScale ignored); the trivial default
    Heightmap, // sampled from a grayscale image (the `heightmap` uuid)
    Noise,     // seeded fBm value noise (reproducible from the params below)
};

// How the layers are mixed across the surface.
enum class TerrainBlend {
    HeightSlope, // procedural auto-blend from each layer's height + slope band (no authoring)
    Splatmap,    // RGBA weights painted into a splat texture (channel i drives layer i)
};

// One material layer: an albedo tiled across the surface, gated by a height + slope band. Generic
// and data-driven (not hardcoded ground/rock/sand/snow), so any biome is expressible as data.
struct TerrainLayer {
    Uuid albedo;                      // albedo texture; unset -> a neutral grey fallback
    AssetHandle<TextureAsset> handle; // runtime resolution of `albedo`, not serialized
    float tiling = 16.0f;             // uv repeats across the whole terrain
    glm::vec2 heightRange{0.0f, 1.0f}; // normalized-height band [min,max] this layer covers
    glm::vec2 slopeRange{0.0f, 1.0f};  // slope band [min,max], 0 = flat .. 1 = vertical
    float sharpness = 0.12f;           // band-edge softness (smoothstep half-width)
};

// A heightmap-or-noise terrain. Plain serializable data: the generated mesh and the resolved layer
// textures live in the TerrainSystem keyed by entity, never here, so this stays trivially copyable
// (snapshots / prefabs / serialize stay simple) and cheap to save. The mesh spans [-size/2, size/2]
// in x/z about the entity origin with y in [0, heightScale], then rides the entity Transform.
struct TerrainComponent {
    float size = 64.0f;       // world units per side (square)
    int resolution = 128;     // vertices per side; (resolution-1)^2 quads
    float heightScale = 10.0f;

    TerrainHeightSource source = TerrainHeightSource::Noise;

    Uuid heightmap; // Heightmap source: a grayscale image, read CPU-side for the heightfield

    // Noise source: seeded fBm value noise (deterministic for a given seed + params).
    std::uint32_t seed = 1337u;
    int octaves = 5;
    float frequency = 2.5f;  // base feature frequency across the terrain
    float lacunarity = 2.0f; // frequency multiplier per octave
    float gain = 0.5f;       // amplitude multiplier per octave

    static constexpr int kMaxLayers = 4;
    std::array<TerrainLayer, kMaxLayers> layers{};
    int layerCount = 1;

    TerrainBlend blend = TerrainBlend::HeightSlope;
    Uuid splat;                            // Splatmap blend: RGBA weights (channel i -> layer i)
    AssetHandle<TextureAsset> splatHandle; // runtime resolution of `splat`, not serialized
};

} // namespace rb
