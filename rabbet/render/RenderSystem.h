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

namespace rb {

class AssetManager;
struct ShaderAsset;

class RenderSystem final : public System {
public:
    void onStart(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;

    // Renders entity indices to an offscreen integer buffer and reads back the entity
    // under (x, y) — viewport pixel coordinates with y measured from the top. Returns
    // an invalid entity when the pixel is empty. Drawn on demand (e.g. on a click).
    [[nodiscard]] Entity pick(Runtime& runtime, int x, int y);

private:
    [[nodiscard]] gl::Mesh* primitiveMesh(PrimitiveShape shape) noexcept;

    // A compiled GL program for a ShaderAsset, cached by the asset's uuid and the source
    // revision it was built from so a hot-reload (revision bump) triggers a recompile.
    struct CompiledProgram {
        std::optional<gl::Shader> program;
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
    std::optional<gl::Shader> m_particle; // billboard sprites for the transparent particle pass
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
};

} // namespace rb
