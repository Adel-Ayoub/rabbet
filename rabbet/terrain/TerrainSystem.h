#pragma once

#include <cstdint>
#include <unordered_map>

#include "rabbet/core/System.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/render/MeshData.h"

namespace rb {

class Scene;
class AssetManager;
struct TerrainComponent;

// Rebuilds each TerrainComponent's heightfield mesh on the CPU when its geometry parameters settle
// (debounced, not every frame) and publishes the meshes + layer bindings into a TerrainRenderData
// resource for the RenderSystem. Runs in the Always phase so terrain previews in the editor without
// entering Play (like lights and particles). The live mesh and build bookkeeping live here keyed by
// entity, never in the component, so the component stays trivially copyable and a scene with no
// terrain does no work at all.
//
// Builds run synchronously when the params settle; resolution is capped so a build stays well under
// a frame. rebuild() is the single seam a worker/job-system would slot behind for very large
// terrains, kept synchronous here to preserve the engine's single-threaded determinism.
class TerrainSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;

private:
    struct Build {
        MeshData mesh;
        std::uint64_t signature = 0; // geometry params the current mesh was built from
        std::uint32_t revision = 0;  // bumped on each rebuild so RenderSystem re-uploads
        float settleTimer = 0.0f;    // debounce: time since the signature last changed
        bool dirty = false;          // signature changed, waiting to settle before rebuilding
        bool built = false;          // a mesh has been built at least once
    };

    void reap(Scene& scene);
    void rebuild(Build& build, const TerrainComponent& terrain, AssetManager* assets);

    std::unordered_map<Entity, Build> m_builds;
};

} // namespace rb
