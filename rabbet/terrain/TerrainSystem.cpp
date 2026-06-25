#include "rabbet/terrain/TerrainSystem.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/terrain/TerrainComponent.h"
#include "rabbet/terrain/TerrainHeightField.h"
#include "rabbet/terrain/TerrainMesh.h"
#include "rabbet/terrain/TerrainRenderData.h"

namespace rb {
namespace {

// Resolution is capped so a synchronous rebuild stays well under a frame; the rebuild() seam is
// where a worker/job-system would slot in to lift this for very large terrains.
constexpr int kMaxResolution = 512;
// Debounce: only rebuild once a geometry parameter has been stable for this long, so dragging a
// slider does not rebuild every frame.
constexpr float kSettleSeconds = 0.12f;
// Clamp a real frame delta so an editor stall does not instantly fire a pending rebuild.
constexpr float kMaxStep = 0.1f;

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 0x100000001b3ULL;
    return hash;
}

std::uint64_t mixFloat(std::uint64_t hash, float value) noexcept {
    return mix(hash, std::bit_cast<std::uint32_t>(value));
}

// A hash of only the parameters that change the geometry, so the mesh rebuilds when (and only when)
// the shape changes. Layer textures, tiling, blend rules and the splat map are pure shader inputs
// and deliberately excluded: editing the look never rebuilds the mesh.
std::uint64_t geometrySignature(const TerrainComponent& terrain) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mixFloat(hash, terrain.size);
    hash = mix(hash, static_cast<std::uint64_t>(terrain.resolution));
    hash = mixFloat(hash, terrain.heightScale);
    hash = mix(hash, static_cast<std::uint64_t>(terrain.source));
    hash = mix(hash, terrain.heightmap.high);
    hash = mix(hash, terrain.heightmap.low);
    hash = mix(hash, terrain.seed);
    hash = mix(hash, static_cast<std::uint64_t>(terrain.octaves));
    hash = mixFloat(hash, terrain.frequency);
    hash = mixFloat(hash, terrain.lacunarity);
    hash = mixFloat(hash, terrain.gain);
    return hash;
}

} // namespace

void TerrainSystem::reap(Scene& scene) {
    for (auto it = m_builds.begin(); it != m_builds.end();) {
        if (!scene.alive(it->first) || scene.tryGet<TerrainComponent>(it->first) == nullptr) {
            it = m_builds.erase(it);
        } else {
            ++it;
        }
    }
}

void TerrainSystem::rebuild(Build& build, const TerrainComponent& terrain, AssetManager* assets) {
    const int resolution = std::clamp(terrain.resolution, 2, kMaxResolution);
    TerrainHeightField field;
    switch (terrain.source) {
    case TerrainHeightSource::Flat:
        field = flatHeightField(resolution);
        break;
    case TerrainHeightSource::Noise:
        field = noiseHeightField(terrain.seed, resolution, terrain.octaves, terrain.frequency,
                                 terrain.lacunarity, terrain.gain);
        break;
    case TerrainHeightSource::Heightmap: {
        bool loaded = false;
        if (assets != nullptr && terrain.heightmap.valid()) {
            if (const std::optional<std::filesystem::path> path =
                    assets->sourcePath(terrain.heightmap)) {
                if (const std::optional<Image> image = loadImage(*path, false)) {
                    field = imageHeightField(*image, resolution);
                    loaded = true;
                }
            }
        }
        if (!loaded) {
            field = flatHeightField(resolution); // unresolved heightmap -> flat until it resolves
        }
        break;
    }
    }
    build.mesh = buildTerrainMesh(field, terrain.size, terrain.heightScale);
    ++build.revision;
}

void TerrainSystem::onUpdate(Runtime& runtime, float dt) {
    Scene& scene = runtime.scene();

    if (scene.count<TerrainComponent>() == 0) {
        // Nothing to build: keep the resource (if any) empty so the render pass stays dormant and a
        // scene with no terrain is byte-identical to before this phase.
        if (TerrainRenderData* data = runtime.tryResource<TerrainRenderData>()) {
            data->clear();
        }
        m_builds.clear();
        return;
    }

    reap(scene);
    AssetManager* assets = runtime.tryResource<AssetManager>();

    if (!runtime.hasResource<TerrainRenderData>()) {
        runtime.addResource<TerrainRenderData>();
    }
    TerrainRenderData& data = runtime.resource<TerrainRenderData>();
    data.clear();

    const float step = std::min(std::max(dt, 0.0f), kMaxStep);

    scene.each<TerrainComponent>([&](Entity entity, TerrainComponent& terrain) {
        Build& build = m_builds[entity];
        const std::uint64_t signature = geometrySignature(terrain);
        if (!build.built) {
            rebuild(build, terrain, assets); // first sight: build now so it appears immediately
            build.signature = signature;
            build.built = true;
            build.dirty = false;
        } else if (signature != build.signature) {
            build.signature = signature; // a geometry param changed: start the debounce window
            build.dirty = true;
            build.settleTimer = 0.0f;
        } else if (build.dirty) {
            build.settleTimer += step;
            if (build.settleTimer >= kSettleSeconds) {
                rebuild(build, terrain, assets);
                build.dirty = false;
            }
        }

        if (build.mesh.vertices.empty()) {
            return; // degenerate (resolution < 2): nothing to draw
        }

        TerrainDraw draw;
        draw.entity = entity;
        draw.mesh = &build.mesh;
        draw.revision = build.revision;
        draw.layerCount = std::clamp(terrain.layerCount, 0, TerrainComponent::kMaxLayers);
        for (int i = 0; i < draw.layerCount; ++i) {
            const TerrainLayer& layer = terrain.layers[static_cast<std::size_t>(i)];
            draw.layers[static_cast<std::size_t>(i)] = TerrainLayerBinding{
                layer.handle, layer.tiling, layer.heightRange, layer.slopeRange, layer.sharpness};
        }
        draw.blend = terrain.blend;
        draw.splat = terrain.blend == TerrainBlend::Splatmap ? terrain.splatHandle
                                                             : AssetHandle<TextureAsset>{};
        draw.heightScale = terrain.heightScale;
        data.draws.push_back(std::move(draw));
    });
}

} // namespace rb
