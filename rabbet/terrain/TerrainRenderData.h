#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/render/MeshData.h"
#include "rabbet/terrain/TerrainComponent.h"

namespace rb {

struct TextureAsset;

// One terrain layer flattened for the renderer: a resolved albedo handle plus its blend rule.
struct TerrainLayerBinding {
    AssetHandle<TextureAsset> albedo; // invalid -> the renderer's neutral grey fallback
    float tiling = 16.0f;
    glm::vec2 heightRange{0.0f, 1.0f};
    glm::vec2 slopeRange{0.0f, 1.0f};
    float sharpness = 0.12f;
};

// One terrain's draw record. The mesh is owned by TerrainSystem (the pointer stays stable across
// frames); `revision` is what RenderSystem compares to know when to re-upload its cached gl::Mesh.
// heightScale lets the shader normalize world height for the height-band blend.
struct TerrainDraw {
    Entity entity;
    const MeshData* mesh = nullptr;
    std::uint32_t revision = 0;
    int layerCount = 0;
    std::array<TerrainLayerBinding, TerrainComponent::kMaxLayers> layers{};
    TerrainBlend blend = TerrainBlend::HeightSlope;
    AssetHandle<TextureAsset> splat; // valid only for the Splatmap blend
    float heightScale = 1.0f;
};

// Published every frame by TerrainSystem, consumed by RenderSystem — the terrain analogue of the
// Lighting / ParticleRenderData resources. Empty -> the terrain pass is skipped entirely, so a
// scene with no TerrainComponent renders byte-identically to before this phase.
struct TerrainRenderData {
    std::vector<TerrainDraw> draws;

    void clear() noexcept { draws.clear(); }
};

} // namespace rb
