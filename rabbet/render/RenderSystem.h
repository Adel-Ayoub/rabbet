#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/System.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/render/gl/DepthMap.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/ParticleStream.h"
#include "rabbet/render/gl/PickBuffer.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/render/gl/UniformBuffer.h"

namespace rb {

class AssetManager;
struct ShaderAsset;

class RenderSystem final : public System {
public:
    void onStart(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;

    // Renders entity indices to an offscreen integer buffer and reads back the entity
    // under (x, y), viewport pixel coordinates with y measured from the top. Returns
    // an invalid entity when the pixel is empty. Drawn on demand (e.g. on a click).
    [[nodiscard]] Entity pick(Runtime& runtime, int x, int y);

private:
    struct FrameContext;

    [[nodiscard]] gl::Mesh* primitiveMesh(PrimitiveShape shape) noexcept;
    void drawShadowMap(FrameContext& frame);
    void drawBuiltInMaterials(FrameContext& frame);
    void drawMaterialComponents(FrameContext& frame);
    void drawTerrain(FrameContext& frame);
    void drawWater(FrameContext& frame);
    void drawParticles(FrameContext& frame);
    void drawDebugColliders(FrameContext& frame);

    // A compiled GL program for a ShaderAsset, cached by the asset's uuid and the source
    // revision it was built from so a hot-reload (revision bump) triggers a recompile.
    struct CompiledProgram {
        std::optional<gl::Shader> program;
        std::uint32_t revision = 0;
    };

    // A terrain entity's uploaded mesh, cached by entity. The TerrainSystem owns the CPU mesh and
    // bumps a revision on every rebuild; this re-uploads only when that revision changes, so a
    // static terrain costs one upload, not one per frame.
    struct TerrainMeshCache {
        std::optional<gl::Mesh> mesh;
        std::uint32_t revision = 0;
    };

    // Returns the cached program for a ShaderAsset, compiling (and reflecting its uniforms into
    // the asset) on first use or after a hot-reload. Returns nullptr if the asset is missing or
    // fails to compile, so the caller can fall back to a built-in program.
    [[nodiscard]] gl::Shader* shaderProgram(AssetManager& assets, AssetHandle<ShaderAsset> handle,
                                            const Uuid& id);

    std::optional<gl::Shader> m_phong;
    std::optional<gl::Shader> m_pbr;
    std::optional<gl::Shader> m_depth;
    std::optional<gl::Shader> m_pick;
    std::optional<gl::Shader> m_flat; // unlit single-colour, for collider wireframes
    std::optional<gl::UniformBuffer> m_cameraUbo; // camera block shared by dialect shader groups
    std::optional<gl::Shader> m_particle; // billboard sprites for the transparent particle pass
    std::optional<gl::Shader> m_terrain;  // lit splat-blended heightfield terrain
    std::optional<gl::Shader> m_water;    // procedurally-waved fresnel water surface
    std::optional<gl::DepthMap> m_shadowMap;
    std::optional<gl::PickBuffer> m_pickBuffer;
    std::optional<gl::Mesh> m_missingMesh;
    std::optional<gl::Texture> m_missingTexture;
    std::optional<gl::Mesh> m_primitiveCube;
    std::optional<gl::Mesh> m_primitiveSphere;
    std::optional<gl::Mesh> m_primitivePlane;
    std::optional<gl::Texture> m_whiteTexture;
    // A 1x1 cubemap kept bound to the environment unit when no irradiance map is present, so the
    // lit shaders' samplerCube is always complete (some drivers validate every sampler per draw).
    std::optional<gl::Cubemap> m_fallbackCubemap;
    // The particle pass: a reusable streaming buffer + a soft round dot used when an emitter has no
    // sprite. The vertex scratch is kept across frames so billboard expansion does not re-allocate.
    std::optional<gl::ParticleStream> m_particleStream;
    std::optional<gl::Texture> m_particleTexture;
    std::vector<gl::ParticleVertex> m_particleVertices;
    std::unordered_map<Uuid, CompiledProgram> m_programCache;
    // A neutral grey bound to every unassigned terrain layer/splat unit so the samplers stay
    // complete (some drivers validate every sampler per draw, even unused ones).
    std::optional<gl::Texture> m_terrainFallback;
    std::optional<gl::Mesh> m_waterMesh; // one unit XZ quad shared by every water surface
    // Unwrapped in double so that each surface can wrap (time * its own speed) onto a whole wave
    // cycle; wrapping this accumulator instead would pop for every speed but 1.
    double m_waterTime = 0.0;
    std::unordered_map<Entity, TerrainMeshCache> m_terrainMeshes;
};

} // namespace rb
