#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/BuiltinShaders.h"
#include "rabbet/render/EnvironmentLighting.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderUniform.h"
#include "rabbet/render/Shadow.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/shaders/GlShaderSources.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/render/DebugDraw.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rabbet/particle/ParticleRenderData.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/WaterComponent.h"
#include "rabbet/terrain/TerrainRenderData.h"

namespace rb {
namespace {

constexpr std::size_t kMaxDirectionalLights = 4;
constexpr std::size_t kMaxPointLights = 8;
constexpr std::size_t kMaxSpotLights = 4;
constexpr int kShadowMapSize = 2048;
constexpr unsigned int kShadowTextureUnit = 1;

glm::mat3 normalMatrix(const glm::mat4& model) {
    return glm::transpose(glm::inverse(glm::mat3(model)));
}

// Builds the material-editable uniform list from a freshly compiled program: every active
// uniform that is not engine-driven (transforms/camera/lights/shadows) and maps to a known
// value kind. This is what the inspector turns into widgets.
std::vector<ShaderUniform> reflectMaterialUniforms(const gl::Shader& shader) {
    std::vector<ShaderUniform> result;
    for (const gl::Shader::ActiveUniform& active : shader.activeUniforms()) {
        if (isEngineUniform(active.name)) {
            continue;
        }
        const UniformType type = uniformTypeFromGlType(active.type);
        if (type == UniformType::Unknown) {
            continue;
        }
        result.push_back(ShaderUniform{active.name, type});
    }
    return result;
}

// Texture unit for the environment irradiance cubemap. Units 0 (albedo) and 1 (shadow) are
// reserved and material textures start at 2, so this sits clear of them (materials may use 2..6).
constexpr unsigned int kEnvTextureUnit = 7;

// Uploads a material's uniform overrides and binds its textures on top of whatever per-surface
// values were already set. Texture units 0 (albedo) and 1 (shadow map) are reserved, so material
// textures start at unit 2. Setting a uniform the program lacks is a silent no-op.
void applyMaterialOverrides(gl::Shader& program, const MaterialAsset& material,
                            AssetManager& assets) {
    for (const MaterialUniform& uniform : material.uniforms) {
        switch (uniform.type) {
        case UniformType::Int:
            program.setInt(uniform.name, uniform.integer);
            break;
        case UniformType::Bool:
            program.setInt(uniform.name, uniform.boolean ? 1 : 0);
            break;
        case UniformType::Float:
            program.setFloat(uniform.name, uniform.vec.x);
            break;
        case UniformType::Vec2:
            program.setVec2(uniform.name, glm::vec2(uniform.vec));
            break;
        case UniformType::Vec3:
            program.setVec3(uniform.name, glm::vec3(uniform.vec));
            break;
        case UniformType::Vec4:
            program.setVec4(uniform.name, uniform.vec);
            break;
        default:
            break; // matrices / samplers are not editable values
        }
    }
    unsigned int unit = 2;
    for (const MaterialTexture& texture : material.textures) {
        if (TextureAsset* asset = assets.get<TextureAsset>(texture.handle)) {
            if (unit >= kEnvTextureUnit) {
                // Out of material slots (2..6). Point the sampler at the albedo unit rather
                // than leaving stale program state; unit 7 would collide with the environment
                // samplerCube and kill the draw.
                program.setInt(texture.name, 0);
                continue;
            }
            asset->texture.bind(unit);
            program.setInt(texture.name, static_cast<int>(unit));
            ++unit;
        }
    }
}

// Sets the per-program environment uniforms. The cubemap itself is bound once per frame (in
// onUpdate) so the lit shaders' samplerCube is always backed by a complete texture, even when
// the environment is off (some drivers validate every sampler per draw regardless of branching).
void uploadEnvironment(gl::Shader& shader, const EnvironmentLight* env) {
    shader.setInt("uIrradiance", static_cast<int>(kEnvTextureUnit));
    shader.setInt("uHasEnvironment", (env != nullptr && env->enabled) ? 1 : 0);
    shader.setFloat("uEnvironmentIntensity", env != nullptr ? env->intensity : 1.0f);
}

void uploadLights(gl::Shader& shader, const glm::mat4& viewProjection, const glm::vec3& viewPosition,
                  const Lighting* lighting, const EnvironmentLight* env, int hdrOutput) {
    shader.setMat4("uViewProjection", viewProjection);
    shader.setVec3("uViewPosition", viewPosition);
    // 1 when a post-process pass will tone-map, so the lit shaders emit linear HDR; 0 keeps the
    // built-in inline tone-map + gamma (a no-op uniform on shaders that lack it, e.g. Phong).
    shader.setInt("uHdrOutput", hdrOutput);
    uploadEnvironment(shader, env);
    if (lighting == nullptr) {
        shader.setVec3("uAmbient", glm::vec3(0.1f));
        shader.setInt("uDirectionalCount", 0);
        shader.setInt("uPointCount", 0);
        shader.setInt("uSpotCount", 0);
        return;
    }
    shader.setVec3("uAmbient", lighting->ambient);
    const auto dirCount = std::min(lighting->directionalDirections.size(), kMaxDirectionalLights);
    shader.setInt("uDirectionalCount", static_cast<int>(dirCount));
    shader.setVec3Array("uDirectionalDirection", {lighting->directionalDirections.data(), dirCount});
    shader.setVec3Array("uDirectionalColor", {lighting->directionalColors.data(), dirCount});
    const auto pointCount = std::min(lighting->pointPositions.size(), kMaxPointLights);
    shader.setInt("uPointCount", static_cast<int>(pointCount));
    shader.setVec3Array("uPointPosition", {lighting->pointPositions.data(), pointCount});
    shader.setVec3Array("uPointColor", {lighting->pointColors.data(), pointCount});
    shader.setVec3Array("uPointAttenuation", {lighting->pointAttenuations.data(), pointCount});
    const auto spotCount = std::min(lighting->spotPositions.size(), kMaxSpotLights);
    shader.setInt("uSpotCount", static_cast<int>(spotCount));
    shader.setVec3Array("uSpotPosition", {lighting->spotPositions.data(), spotCount});
    shader.setVec3Array("uSpotDirection", {lighting->spotDirections.data(), spotCount});
    shader.setVec3Array("uSpotColor", {lighting->spotColors.data(), spotCount});
    shader.setVec3Array("uSpotAttenuation", {lighting->spotAttenuations.data(), spotCount});
    shader.setVec2Array("uSpotCone", {lighting->spotCones.data(), spotCount});
}

// Rewrites `values` to hold only the entries at `keep`, in that order; the companion of
// selectNearestLights for the parallel per-light arrays.
template <typename T>
void keepIndices(std::vector<T>& values, const std::vector<std::size_t>& keep) {
    std::vector<T> kept;
    kept.reserve(keep.size());
    for (const std::size_t index : keep) {
        kept.push_back(values[index]);
    }
    values = std::move(kept);
}

// A soft round dot used as the default particle sprite when an emitter has no texture: white with
// a smooth radial alpha falloff, so additive sparks glow and alpha puffs feather at the edges.
gl::Texture buildSoftParticleTexture() {
    constexpr int kSize = 64;
    std::vector<std::byte> pixels(static_cast<std::size_t>(kSize) * kSize * 4);
    const float center = (kSize - 1) * 0.5f;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = (static_cast<float>(x) - center) / center;
            const float dy = (static_cast<float>(y) - center) / center;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float t = std::max(0.0f, 1.0f - d * d);
            const auto alpha = static_cast<std::uint8_t>(std::min(1.0f, t * t) * 255.0f + 0.5f);
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kSize) +
                                   static_cast<std::size_t>(x)) *
                                  4u;
            pixels[i + 0] = std::byte{255};
            pixels[i + 1] = std::byte{255};
            pixels[i + 2] = std::byte{255};
            pixels[i + 3] = std::byte{alpha};
        }
    }
    gl::TextureConfig config;
    config.srgb = false;
    config.generateMipmaps = true;
    config.linearFilter = true;
    config.repeat = false; // clamp so the quad edge does not wrap the falloff
    return gl::Texture::fromPixels(pixels, kSize, kSize, 4, config);
}

} // namespace

gl::Mesh* RenderSystem::primitiveMesh(PrimitiveShape shape) noexcept {
    switch (shape) {
        case PrimitiveShape::Cube: return m_primitiveCube ? &*m_primitiveCube : nullptr;
        case PrimitiveShape::Sphere: return m_primitiveSphere ? &*m_primitiveSphere : nullptr;
        case PrimitiveShape::Plane: return m_primitivePlane ? &*m_primitivePlane : nullptr;
    }
    return nullptr;
}

gl::Shader* RenderSystem::shaderProgram(AssetManager& assets, AssetHandle<ShaderAsset> handle,
                                        const Uuid& id) {
    ShaderAsset* asset = assets.get<ShaderAsset>(handle);
    if (asset == nullptr) {
        return nullptr;
    }
    if (const auto it = m_programCache.find(id);
        it != m_programCache.end() && it->second.revision == asset->revision) {
        return it->second.program ? &*it->second.program : nullptr;
    }
    std::optional<gl::Shader> compiled =
        gl::Shader::fromSource(asset->vertexSource, asset->fragmentSource);
    if (compiled) {
        asset->uniforms = reflectMaterialUniforms(*compiled);
    } else {
        log::error("render system: failed to compile shader asset");
    }
    // Record the revision even on failure so a broken shader is not recompiled every frame; the
    // next hot-reload (revision bump) retries.
    CompiledProgram entry;
    entry.revision = asset->revision;
    entry.program = std::move(compiled);
    const auto [pos, inserted] = m_programCache.insert_or_assign(id, std::move(entry));
    (void)inserted;
    return pos->second.program ? &*pos->second.program : nullptr;
}

void RenderSystem::onStart(Runtime& runtime) {
    // Register the built-in shaders + default material so a MaterialComponent can reference the
    // defaults without any asset file (and so examples that add an AssetManager get them too).
    if (AssetManager* assets = runtime.tryResource<AssetManager>()) {
        registerDefaultRenderAssets(*assets);
    }
    m_phong = gl::Shader::fromSource(builtinLitVertexSource(), builtinPhongFragmentSource());
    if (!m_phong) {
        log::error("render system: failed to build the Phong shader");
    }
    m_pbr = gl::Shader::fromSource(builtinLitVertexSource(), builtinPbrFragmentSource());
    if (!m_pbr) {
        log::error("render system: failed to build the PBR shader");
    }
    m_depth = gl::Shader::fromSource(shaders::kDepthVertex, shaders::kDepthFragment);
    if (!m_depth) {
        log::error("render system: failed to build the depth shader");
    }
    m_pick = gl::Shader::fromSource(shaders::kPickVertex, shaders::kPickFragment);
    if (!m_pick) {
        log::error("render system: failed to build the pick shader");
    }
    m_flat = gl::Shader::fromSource(shaders::kFlatVertex, shaders::kFlatFragment);
    if (!m_flat) {
        log::error("render system: failed to build the flat shader");
    }
    // The CPU expands every particle into a camera facing quad before upload, so the
    // particle vertex stage is a bare transform, and the blend mode belongs to the
    // render pass rather than the fragment shader.
    m_particle = gl::Shader::fromSource(shaders::kParticleVertex, shaders::kParticleFragment);
    if (!m_particle) {
        log::error("render system: failed to build the particle shader");
    }
    m_terrain = gl::Shader::fromSource(builtinTerrainVertexSource(), builtinTerrainFragmentSource());
    if (!m_terrain) {
        log::error("render system: failed to build the terrain shader");
    }
    m_water = gl::Shader::fromSource(builtinWaterVertexSource(), builtinWaterFragmentSource());
    if (!m_water) {
        log::error("render system: failed to build the water shader");
    }
    MeshData waterQuad;
    waterQuad.vertices = {
        {{-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };
    waterQuad.indices = {0, 1, 2, 0, 2, 3};
    m_waterMesh = gl::Mesh::create(waterQuad);
    m_terrainFallback = gl::Texture::solid(128, 128, 128); // neutral grey for unassigned layers
    m_particleStream = gl::ParticleStream::create();
    m_particleTexture = buildSoftParticleTexture();
    m_shadowMap = gl::DepthMap::create(kShadowMapSize, kShadowMapSize);
    m_missingMesh = gl::Mesh::create(geometry::cube());
    m_missingTexture = gl::Texture::solid(255, 0, 255);
    m_primitiveCube = gl::Mesh::create(geometry::cube());
    m_primitiveSphere = gl::Mesh::create(geometry::sphere());
    m_primitivePlane = gl::Mesh::create(geometry::quad());
    m_whiteTexture = gl::Texture::solid(255, 255, 255);
    m_fallbackCubemap = gl::Cubemap::empty(1); // keeps the environment sampler complete when off
}

void RenderSystem::onUpdate(Runtime& runtime, float dt) {
    // Clamped like the particle step: a blocking dialog or a scene load hands back a multi-second
    // frame, which would otherwise snap every wave to a new phase.
    m_waterTime += static_cast<double>(std::min(std::max(dt, 0.0f), 0.1f));
    if (!runtime.hasResource<RenderView>()) {
        return;
    }
    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;
    const Lighting* lighting = runtime.tryResource<Lighting>();
    const EnvironmentLight* environment = runtime.tryResource<EnvironmentLight>();

    // Over the shader caps, keep the lights nearest the view (selected once per frame,
    // every uploadLights call below reads the clamped copy). Directional lights have no
    // position, so an over-cap set of those stays authored-order.
    Lighting selected;
    if (lighting != nullptr && (lighting->pointPositions.size() > kMaxPointLights ||
                                lighting->spotPositions.size() > kMaxSpotLights)) {
        selected = *lighting;
        if (selected.pointPositions.size() > kMaxPointLights) {
            const std::vector<std::size_t> keep =
                selectNearestLights(selected.pointPositions, view.position, kMaxPointLights);
            keepIndices(selected.pointPositions, keep);
            keepIndices(selected.pointColors, keep);
            keepIndices(selected.pointAttenuations, keep);
        }
        if (selected.spotPositions.size() > kMaxSpotLights) {
            const std::vector<std::size_t> keep =
                selectNearestLights(selected.spotPositions, view.position, kMaxSpotLights);
            keepIndices(selected.spotPositions, keep);
            keepIndices(selected.spotDirections, keep);
            keepIndices(selected.spotColors, keep);
            keepIndices(selected.spotAttenuations, keep);
            keepIndices(selected.spotCones, keep);
        }
        lighting = &selected;
    }

    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets != nullptr) {
        // Safety net: resolve any ModelRenderer not yet linked to its asset, in case
        // no AssetResolveSystem is registered. That system also re-resolves every
        // frame to follow hot reloads; this only fills in still-invalid handles.
        runtime.scene().each<ModelRenderer>([assets](Entity, ModelRenderer& renderer) {
            if (!renderer.handle.valid()) {
                renderer.handle = assets->find<ModelAsset>(renderer.model);
            }
        });
    }

    Scene& scene = runtime.scene();

    // When a post-process pass is active the lit shaders emit linear HDR (the post chain tone-maps);
    // otherwise they keep their inline tone-map + gamma so the direct path is byte-identical.
    const int hdrOutput = activePostProcess(scene) != nullptr ? 1 : 0;

    // An entity whose material has resolved is drawn by the material pass below; the built-in
    // passes skip it so its geometry is not drawn twice.
    const auto materialDriven = [&scene](Entity e) {
        const MaterialComponent* component = scene.tryGet<MaterialComponent>(e);
        return component != nullptr && component->handle.valid();
    };

    // Sets per-entity and per-submesh uniforms and draws a model, applying a material's overrides
    // on top of each submesh when one is supplied. Shared by the built-in ModelRenderer pass (no
    // overrides) and the material pass so the two cannot drift: a default material renders a model
    // identically to the built-in path.
    const auto drawModelSubmeshes = [this, assets](gl::Shader& program, const glm::mat4& world,
                                                   ModelAsset* model,
                                                   const MaterialAsset* overrides) {
        program.setMat4("uModel", world);
        program.setMat3("uNormalMatrix", normalMatrix(world));
        if (model == nullptr) {
            // unresolved or missing model: draw a loud magenta placeholder, but only when both
            // fallback resources exist (else skip this entity).
            if (m_missingMesh && m_missingTexture) {
                program.setVec3("uBaseColor", glm::vec3(1.0f, 0.0f, 1.0f));
                program.setVec3("uEmissive", glm::vec3(0.0f));
                program.setFloat("uMetallic", 0.0f);
                program.setFloat("uRoughness", 1.0f);
                program.setFloat("uAo", 1.0f);
                m_missingTexture->bind(0);
                if (overrides != nullptr && assets != nullptr) {
                    applyMaterialOverrides(program, *overrides, *assets);
                }
                m_missingMesh->draw();
            }
            return;
        }
        for (const ModelAsset::Submesh& submesh : model->submeshes) {
            program.setVec3("uBaseColor", submesh.baseColor);
            program.setVec3("uEmissive", submesh.emissive);
            program.setFloat("uMetallic", submesh.metallic);
            program.setFloat("uRoughness", submesh.roughness);
            program.setFloat("uAo", submesh.ao);
            if (assets != nullptr) {
                if (TextureAsset* texture = assets->get<TextureAsset>(submesh.albedo)) {
                    texture->texture.bind(0);
                } else if (m_missingTexture) {
                    m_missingTexture->bind(0);
                }
            }
            if (overrides != nullptr && assets != nullptr) {
                applyMaterialOverrides(program, *overrides, *assets);
            }
            submesh.mesh.draw();
        }
    };

    const bool shadows = m_depth.has_value() && m_shadowMap.has_value() && lighting != nullptr &&
                         !lighting->directionalDirections.empty();
    glm::mat4 lightSpace{1.0f};

    if (shadows) {
        // Remember the target the caller had bound (the default framebuffer, or an
        // editor offscreen framebuffer) so we can restore it after the shadow pass.
        // The old code hard-bound framebuffer 0 and only reset the viewport when a
        // Viewport resource existed, which left the main pass at the shadow map's size.
        GLint prevFramebuffer = 0;
        GLint prevViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        lightSpace = directionalLightSpace(lighting->directionalDirections.front(), 7.0f, 1.0f,
                                           30.0f, 14.0f);
        m_shadowMap->bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);
        m_depth->bind();
        m_depth->setMat4("uLightSpace", lightSpace);
        runtime.scene().each<WorldMatrix, gl::Mesh>([this](Entity, WorldMatrix& world, gl::Mesh& mesh) {
            m_depth->setMat4("uModel", world.value);
            mesh.draw();
        });
        if (assets != nullptr) {
            runtime.scene().each<WorldMatrix, ModelRenderer>(
                [this, assets](Entity, WorldMatrix& world, ModelRenderer& renderer) {
                    m_depth->setMat4("uModel", world.value);
                    const ModelAsset* model = assets->get<ModelAsset>(renderer.handle);
                    if (model == nullptr) {
                        if (m_missingMesh) {
                            m_missingMesh->draw();
                        }
                        return;
                    }
                    for (const ModelAsset::Submesh& submesh : model->submeshes) {
                        submesh.mesh.draw();
                    }
                });
        }
        runtime.scene().each<WorldMatrix, Primitive>(
            [this](Entity, WorldMatrix& world, Primitive& primitive) {
                if (gl::Mesh* mesh = primitiveMesh(primitive.shape)) {
                    m_depth->setMat4("uModel", world.value);
                    mesh->draw();
                }
            });
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        m_shadowMap->bindTexture(kShadowTextureUnit);
    }

    const int hasShadow = shadows ? 1 : 0;

    // Bind the environment irradiance (or a 1x1 fallback) to its reserved unit for the whole frame
    // so every lit program's samplerCube stays complete; uHasEnvironment gates whether it is used.
    if (const gl::Cubemap* envCube =
            environment != nullptr ? &environment->irradiance
                                   : (m_fallbackCubemap ? &*m_fallbackCubemap : nullptr)) {
        envCube->bind(kEnvTextureUnit);
    }

    if (m_phong && runtime.scene().count<Material>() > 0) {
        m_phong->bind();
        uploadLights(*m_phong, viewProjection, view.position, lighting, environment, hdrOutput);
        m_phong->setInt("uTexture", 0);
        m_phong->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_phong->setInt("uHasShadowMap", hasShadow);
        m_phong->setMat4("uLightSpace", lightSpace);
        m_phong->setVec3("uEmissive", glm::vec3(0.0f));
        runtime.scene().each<WorldMatrix, gl::Mesh, Material>(
            [this, &materialDriven](Entity e, WorldMatrix& world, gl::Mesh& mesh, Material& material) {
                if (materialDriven(e)) {
                    return; // shaded by the material pass instead
                }
                m_phong->setMat4("uModel", world.value);
                m_phong->setMat3("uNormalMatrix", normalMatrix(world.value));
                m_phong->setVec3("uTint", material.tint);
                m_phong->setFloat("uSpecularStrength", material.specular);
                m_phong->setFloat("uShininess", material.shininess);
                material.texture.bind(0);
                mesh.draw();
            });
    }

    if (m_pbr && runtime.scene().count<PbrMaterial>() > 0) {
        m_pbr->bind();
        uploadLights(*m_pbr, viewProjection, view.position, lighting, environment, hdrOutput);
        m_pbr->setInt("uAlbedoTex", 0);
        m_pbr->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_pbr->setInt("uHasShadowMap", hasShadow);
        m_pbr->setMat4("uLightSpace", lightSpace);
        runtime.scene().each<WorldMatrix, gl::Mesh, PbrMaterial>(
            [this, &materialDriven](Entity e, WorldMatrix& world, gl::Mesh& mesh,
                                    PbrMaterial& material) {
                if (materialDriven(e)) {
                    return; // shaded by the material pass instead
                }
                m_pbr->setMat4("uModel", world.value);
                m_pbr->setMat3("uNormalMatrix", normalMatrix(world.value));
                m_pbr->setVec3("uBaseColor", material.baseColor);
                m_pbr->setVec3("uEmissive", material.emissive);
                m_pbr->setFloat("uMetallic", material.metallic);
                m_pbr->setFloat("uRoughness", material.roughness);
                m_pbr->setFloat("uAo", material.ao);
                material.albedo.bind(0);
                mesh.draw();
            });
    }

    if (m_pbr && assets != nullptr && runtime.scene().count<ModelRenderer>() > 0) {
        m_pbr->bind();
        uploadLights(*m_pbr, viewProjection, view.position, lighting, environment, hdrOutput);
        m_pbr->setInt("uAlbedoTex", 0);
        m_pbr->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_pbr->setInt("uHasShadowMap", hasShadow);
        m_pbr->setMat4("uLightSpace", lightSpace);
        runtime.scene().each<WorldMatrix, ModelRenderer>(
            [this, assets, &materialDriven, &drawModelSubmeshes](Entity e, WorldMatrix& world,
                                                                 ModelRenderer& renderer) {
                if (materialDriven(e)) {
                    return; // shaded by the material pass instead
                }
                ModelAsset* model = assets->get<ModelAsset>(renderer.handle);
                drawModelSubmeshes(*m_pbr, world.value, model, nullptr);
            });
    }

    if (m_pbr && m_whiteTexture && runtime.scene().count<Primitive>() > 0) {
        m_pbr->bind();
        uploadLights(*m_pbr, viewProjection, view.position, lighting, environment, hdrOutput);
        m_pbr->setInt("uAlbedoTex", 0);
        m_pbr->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_pbr->setInt("uHasShadowMap", hasShadow);
        m_pbr->setMat4("uLightSpace", lightSpace);
        m_whiteTexture->bind(0);
        runtime.scene().each<WorldMatrix, Primitive>(
            [this, &materialDriven](Entity e, WorldMatrix& world, Primitive& primitive) {
                if (materialDriven(e)) {
                    return; // shaded by the material pass instead
                }
                gl::Mesh* mesh = primitiveMesh(primitive.shape);
                if (mesh == nullptr) {
                    return;
                }
                m_pbr->setMat4("uModel", world.value);
                m_pbr->setMat3("uNormalMatrix", normalMatrix(world.value));
                m_pbr->setVec3("uBaseColor", primitive.color);
                m_pbr->setVec3("uEmissive", primitive.emissive);
                m_pbr->setFloat("uMetallic", primitive.metallic);
                m_pbr->setFloat("uRoughness", primitive.roughness);
                m_pbr->setFloat("uAo", 1.0f);
                mesh->draw();
            });
    }

    // Data-driven material pass: entities with a resolved MaterialComponent are shaded by their
    // material's shader (the built-in passes above skipped them). The material's uniform/texture
    // overrides apply on top of the per-surface values, so the default PBR material renders a
    // model exactly like the built-in path; a custom shader or overrides change the look.
    if (assets != nullptr && runtime.scene().count<MaterialComponent>() > 0) {
        runtime.scene().each<WorldMatrix, MaterialComponent>(
            [&](Entity e, WorldMatrix& world, MaterialComponent& component) {
                if (!component.handle.valid()) {
                    return; // unresolved: left to the built-in fallback passes
                }
                MaterialAsset* material = assets->get<MaterialAsset>(component.handle);
                if (material == nullptr) {
                    return;
                }
                gl::Shader* program =
                    shaderProgram(*assets, material->shaderHandle, material->shader);
                if (program == nullptr) {
                    program = m_pbr ? &*m_pbr : nullptr; // fall back so the entity still draws
                }
                if (program == nullptr) {
                    return;
                }
                program->bind();
                uploadLights(*program, viewProjection, view.position, lighting, environment, hdrOutput);
                program->setInt("uAlbedoTex", 0);
                program->setInt("uTexture", 0);
                program->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
                program->setInt("uHasShadowMap", hasShadow);
                program->setMat4("uLightSpace", lightSpace);

                if (ModelRenderer* renderer = scene.tryGet<ModelRenderer>(e)) {
                    ModelAsset* model = assets->get<ModelAsset>(renderer->handle);
                    drawModelSubmeshes(*program, world.value, model, material);
                } else if (Primitive* primitive = scene.tryGet<Primitive>(e)) {
                    if (gl::Mesh* mesh = primitiveMesh(primitive->shape)) {
                        if (m_whiteTexture) {
                            m_whiteTexture->bind(0);
                        }
                        program->setMat4("uModel", world.value);
                        program->setMat3("uNormalMatrix", normalMatrix(world.value));
                        program->setVec3("uBaseColor", primitive->color);
                        program->setVec3("uEmissive", primitive->emissive);
                        program->setFloat("uMetallic", primitive->metallic);
                        program->setFloat("uRoughness", primitive->roughness);
                        program->setFloat("uAo", 1.0f);
                        applyMaterialOverrides(*program, *material, *assets);
                        mesh->draw();
                    }
                } else if (gl::Mesh* mesh = scene.tryGet<gl::Mesh>(e)) {
                    if (m_whiteTexture) {
                        m_whiteTexture->bind(0);
                    }
                    program->setMat4("uModel", world.value);
                    program->setMat3("uNormalMatrix", normalMatrix(world.value));
                    program->setVec3("uBaseColor", glm::vec3(1.0f));
                    program->setVec3("uEmissive", glm::vec3(0.0f));
                    program->setFloat("uMetallic", 0.0f);
                    program->setFloat("uRoughness", 0.8f);
                    program->setFloat("uAo", 1.0f);
                    applyMaterialOverrides(*program, *material, *assets);
                    mesh->draw();
                }
            });
    }

    // Terrain pass: lit, splat-blended heightfields published by TerrainSystem. Opaque (depth-write
    // on), drawn after the other opaque passes. Each entity's CPU mesh is uploaded once and cached,
    // re-uploaded only when its revision changes. Runs only when a terrain published a draw, so a
    // scene with no TerrainComponent is byte-identical to before this phase (the resource is absent).
    if (const TerrainRenderData* terrain = runtime.tryResource<TerrainRenderData>();
        m_terrain && m_terrainFallback && terrain != nullptr && !terrain->draws.empty()) {
        constexpr unsigned int kLayerUnit0 = 2; // units 0 (albedo) / 1 (shadow) / 7 (env) reserved
        constexpr unsigned int kSplatUnit = 6;
        const gl::Texture& fallback = *m_terrainFallback;

        m_terrain->bind();
        uploadLights(*m_terrain, viewProjection, view.position, lighting, environment, hdrOutput);
        m_terrain->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_terrain->setInt("uHasShadowMap", hasShadow);
        m_terrain->setMat4("uLightSpace", lightSpace);
        m_terrain->setFloat("uMetallic", 0.0f);
        m_terrain->setFloat("uRoughness", 0.92f);
        for (int i = 0; i < 4; ++i) {
            m_terrain->setInt("uLayerAlbedo[" + std::to_string(i) + "]",
                              static_cast<int>(kLayerUnit0) + i);
        }
        m_terrain->setInt("uSplat", static_cast<int>(kSplatUnit));

        for (const TerrainDraw& draw : terrain->draws) {
            const WorldMatrix* world = scene.tryGet<WorldMatrix>(draw.entity);
            if (draw.mesh == nullptr || world == nullptr) {
                continue;
            }
            TerrainMeshCache& cache = m_terrainMeshes[draw.entity];
            if (!cache.mesh.has_value() || cache.revision != draw.revision) {
                cache.mesh = gl::Mesh::create(*draw.mesh);
                cache.revision = draw.revision;
            }

            m_terrain->setMat4("uModel", world->value);
            m_terrain->setMat3("uNormalMatrix", normalMatrix(world->value));
            m_terrain->setFloat("uHeightScale", draw.heightScale);
            m_terrain->setInt("uLayerCount", draw.layerCount);
            m_terrain->setInt("uBlendMode", draw.blend == TerrainBlend::Splatmap ? 1 : 0);

            for (int i = 0; i < 4; ++i) {
                const std::string idx = "[" + std::to_string(i) + "]";
                const gl::Texture* tex = &fallback;
                glm::vec2 heightRange{0.0f, 1.0f};
                glm::vec2 slopeRange{0.0f, 1.0f};
                float tiling = 1.0f;
                float sharpness = 0.12f;
                if (i < draw.layerCount) {
                    const TerrainLayerBinding& layer = draw.layers[static_cast<std::size_t>(i)];
                    if (assets != nullptr && layer.albedo.valid()) {
                        if (TextureAsset* asset = assets->get<TextureAsset>(layer.albedo)) {
                            tex = &asset->texture;
                        }
                    }
                    heightRange = layer.heightRange;
                    slopeRange = layer.slopeRange;
                    tiling = layer.tiling;
                    sharpness = layer.sharpness;
                }
                tex->bind(kLayerUnit0 + static_cast<unsigned int>(i));
                m_terrain->setFloat("uLayerTiling" + idx, tiling);
                m_terrain->setVec2("uLayerHeightRange" + idx, heightRange);
                m_terrain->setVec2("uLayerSlopeRange" + idx, slopeRange);
                m_terrain->setFloat("uLayerSharpness" + idx, sharpness);
            }

            const gl::Texture* splat = &fallback;
            int hasSplat = 0;
            if (draw.blend == TerrainBlend::Splatmap && assets != nullptr && draw.splat.valid()) {
                if (TextureAsset* asset = assets->get<TextureAsset>(draw.splat)) {
                    splat = &asset->texture;
                    hasSplat = 1;
                }
            }
            splat->bind(kSplatUnit);
            m_terrain->setInt("uHasSplat", hasSplat);

            cache.mesh->draw();
        }

        // Release cached meshes for terrains no longer drawn (entity destroyed or component removed).
        for (auto it = m_terrainMeshes.begin(); it != m_terrainMeshes.end();) {
            const bool present = std::any_of(
                terrain->draws.begin(), terrain->draws.end(),
                [&](const TerrainDraw& draw) { return draw.entity == it->first; });
            if (present) {
                ++it;
            } else {
                it = m_terrainMeshes.erase(it);
            }
        }
    } else if (!m_terrainMeshes.empty()) {
        m_terrainMeshes.clear();
    }

    // Water pass: procedurally-waved fresnel surfaces, one shared unit quad per component.
    // Transparent like the particles (depth-tested, writes off, over-blend) and drawn before
    // them so sparks stay visible above the surface. Runs only when a scene has an enabled
    // component, so every other scene is byte-identical to before this phase.
    if (m_water && m_waterMesh && scene.count<WaterComponent>() > 0) {
        // Every octave completes a whole number of cycles over this span, so wrapping (time *
        // speed) onto it is seamless for any authored speed, not just the default.
        constexpr double kWavePeriod = 8.0 * 3.14159265358979323846;
        const Skybox* sky = runtime.tryResource<Skybox>();
        constexpr unsigned int kWaterSkyUnit = 8; // past the terrain's reserved 0..7 set
        bool passOpen = false;
        GLboolean wasBlend = GL_FALSE;
        GLboolean wasCull = GL_FALSE;
        GLboolean depthWrite = GL_TRUE;
        GLint blendSrcRgb = GL_ONE;
        GLint blendDstRgb = GL_ZERO;
        GLint blendSrcAlpha = GL_ONE;
        GLint blendDstAlpha = GL_ZERO;
        GLint activeUnit = GL_TEXTURE0;

        scene.each<WaterComponent, WorldMatrix>([&](Entity, WaterComponent& w, WorldMatrix& world) {
            if (!w.enabled) {
                return;
            }
            if (!passOpen) {
                // Opened lazily on the first surface actually drawn, so a scene whose water is
                // all disabled touches no GL state at all.
                passOpen = true;
                wasBlend = glIsEnabled(GL_BLEND);
                wasCull = glIsEnabled(GL_CULL_FACE);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
                glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
                glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
                glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
                glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE); // the surface reads from below as well

                m_water->bind();
                uploadLights(*m_water, viewProjection, view.position, lighting, environment,
                             hdrOutput);
                m_water->setInt("uSkybox", static_cast<int>(kWaterSkyUnit));
                m_water->setInt("uHasSkybox", sky != nullptr ? 1 : 0);
                // Bound either way: an incomplete samplerCube can cost the whole draw on drivers
                // that validate every sampler, which is why the fallback exists.
                if (sky != nullptr) {
                    sky->cubemap.bind(kWaterSkyUnit);
                } else if (m_fallbackCubemap) {
                    m_fallbackCubemap->bind(kWaterSkyUnit);
                }
            }

            WaterComponent safe = w;
            sanitizeWater(safe);
            const float phase =
                static_cast<float>(std::fmod(m_waterTime * static_cast<double>(safe.waveSpeed),
                                             kWavePeriod));
            m_water->setMat4("uModel", waterSurfaceModel(world.value, safe.extent));
            m_water->setFloat("uTime", phase);
            m_water->setVec2("uExtent", safe.extent);
            m_water->setFloat("uWaveTileScale", safe.waveTileScale);
            m_water->setFloat("uWaveStrength", safe.waveStrength);
            m_water->setFloat("uSmoothness", safe.smoothness);
            m_water->setVec4("uDeepColor", safe.deepColor);
            m_water->setVec4("uShallowColor", safe.shallowColor);
            m_waterMesh->draw();
        });

        if (passOpen) {
            glActiveTexture(static_cast<GLenum>(activeUnit));
            glDepthMask(depthWrite);
            glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
                                static_cast<GLenum>(blendSrcAlpha),
                                static_cast<GLenum>(blendDstAlpha));
            if (wasCull == GL_TRUE) {
                glEnable(GL_CULL_FACE);
            }
            if (wasBlend == GL_FALSE) {
                glDisable(GL_BLEND);
            }
        }
    }

    // Transparent particle pass: camera-facing billboards drawn after the opaque + material passes,
    // depth-tested against the scene (so geometry occludes them) but with depth writes off so they
    // blend with each other instead of z-fighting. Additive sums light (glow), Alpha is a standard
    // over-blend. Runs only when an emitter published particles, so a scene without one is
    // byte-identical. State touched here is saved and restored so the pass is order-independent.
    if (const ParticleRenderData* particles = runtime.tryResource<ParticleRenderData>();
        m_particle && m_particleStream && m_particleTexture && particles != nullptr &&
        !particles->batches.empty()) {
        const glm::mat4& v = view.view;
        const glm::vec3 camRight{v[0][0], v[1][0], v[2][0]};
        const glm::vec3 camUp{v[0][1], v[1][1], v[2][1]};

        const GLboolean wasBlend = glIsEnabled(GL_BLEND);
        const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthWrite = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        // The batches switch the blend func per blend mode; capture it so the pass leaves
        // the function exactly as found, not stuck on whatever the last batch used.
        GLint blendSrcRgb = GL_ONE;
        GLint blendDstRgb = GL_ZERO;
        GLint blendSrcAlpha = GL_ONE;
        GLint blendDstAlpha = GL_ZERO;
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE); // billboards are double-sided; winding must not cull them

        m_particle->bind();
        m_particle->setMat4("uViewProjection", viewProjection);
        m_particle->setInt("uTexture", 0);

        for (const ParticleDrawBatch& batch : particles->batches) {
            if (batch.particles.empty()) {
                continue;
            }
            if (batch.blendMode == ParticleBlendMode::Additive) {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            } else {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }

            const gl::Texture* sprite = m_particleTexture ? &*m_particleTexture : nullptr;
            if (assets != nullptr && batch.sprite.valid()) {
                if (TextureAsset* texture = assets->get<TextureAsset>(batch.sprite)) {
                    sprite = &texture->texture;
                }
            }
            if (sprite != nullptr) {
                sprite->bind(0);
            }

            m_particleVertices.clear();
            m_particleVertices.reserve(batch.particles.size() * 6);
            for (const ParticleBillboard& p : batch.particles) {
                const glm::vec3 right = camRight * (p.size * 0.5f);
                const glm::vec3 up = camUp * (p.size * 0.5f);
                const glm::vec3 bl = p.position - right - up;
                const glm::vec3 br = p.position + right - up;
                const glm::vec3 tr = p.position + right + up;
                const glm::vec3 tl = p.position - right + up;
                m_particleVertices.push_back({bl, {0.0f, 0.0f}, p.color});
                m_particleVertices.push_back({br, {1.0f, 0.0f}, p.color});
                m_particleVertices.push_back({tr, {1.0f, 1.0f}, p.color});
                m_particleVertices.push_back({bl, {0.0f, 0.0f}, p.color});
                m_particleVertices.push_back({tr, {1.0f, 1.0f}, p.color});
                m_particleVertices.push_back({tl, {0.0f, 1.0f}, p.color});
            }
            m_particleStream->upload(m_particleVertices);
            m_particleStream->draw();
        }

        glDepthMask(depthWrite);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
                            static_cast<GLenum>(blendSrcAlpha),
                            static_cast<GLenum>(blendDstAlpha));
        if (wasCull == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        }
        if (wasBlend == GL_FALSE) {
            glDisable(GL_BLEND);
        }
    }

    // Collider debug wireframes (toggled by the editor). Drawn unlit, depth-test off so the
    // outlines read as gizmos sitting over the shaded scene rather than being occluded by it.
    if (const DebugDraw* debug = runtime.tryResource<DebugDraw>();
        m_flat && debug != nullptr && debug->colliders) {
        m_flat->bind();
        m_flat->setMat4("uViewProjection", viewProjection);
        m_flat->setVec3("uColor", glm::vec3(0.25f, 0.95f, 0.40f));
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        // Outlines compose the world pose like the bodies do (PhysicsSystem spawns at
        // worldPoseOf + rotated offset): a parented collider's wireframe must sit where
        // the body actually collides, not at the raw local TRS. Collider dimensions stay
        // world-space, so the composed scale is deliberately not applied to them.
        Scene& wireScene = runtime.scene();
        wireScene.each<Transform, BoxCollider>(
            [this, &wireScene](Entity entity, Transform&, BoxCollider& box) {
                if (!m_primitiveCube) {
                    return;
                }
                const WorldPose pose = worldPoseOf(wireScene, entity);
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), pose.position) *
                                        glm::mat4_cast(pose.rotation) *
                                        glm::translate(glm::mat4(1.0f), box.offset) *
                                        glm::scale(glm::mat4(1.0f), box.halfExtents * 2.0f);
                m_flat->setMat4("uModel", model);
                m_primitiveCube->draw();
            });
        wireScene.each<Transform, SphereCollider>(
            [this, &wireScene](Entity entity, Transform&, SphereCollider& sphere) {
                if (!m_primitiveSphere) {
                    return;
                }
                const WorldPose pose = worldPoseOf(wireScene, entity);
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), pose.position) *
                                        glm::mat4_cast(pose.rotation) *
                                        glm::translate(glm::mat4(1.0f), sphere.offset) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(sphere.radius * 2.0f));
                m_flat->setMat4("uModel", model);
                m_primitiveSphere->draw();
            });
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_DEPTH_TEST);
    }
}

Entity RenderSystem::pick(Runtime& runtime, int x, int y) {
    if (!m_pick || !runtime.hasResource<RenderView>() || !runtime.hasResource<Viewport>()) {
        return Entity{};
    }
    const Viewport& viewport = runtime.resource<Viewport>();
    const int width = std::max(1, viewport.width);
    const int height = std::max(1, viewport.height);
    if (!m_pickBuffer.has_value()) {
        m_pickBuffer = gl::PickBuffer::create(width, height);
    } else {
        m_pickBuffer->resize(width, height);
    }

    GLint prevFramebuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;

    m_pickBuffer->bindAndClear();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    const GLboolean pickWasBlend = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND); // integer colour targets cannot be blended

    m_pick->bind();
    m_pick->setMat4("uViewProjection", viewProjection);

    runtime.scene().each<WorldMatrix, Primitive>(
        [this](Entity e, WorldMatrix& world, Primitive& primitive) {
            if (gl::Mesh* mesh = primitiveMesh(primitive.shape)) {
                m_pick->setMat4("uModel", world.value);
                m_pick->setInt("uEntityId", static_cast<int>(e.index()));
                mesh->draw();
            }
        });

    if (AssetManager* assets = runtime.tryResource<AssetManager>()) {
        runtime.scene().each<WorldMatrix, ModelRenderer>(
            [this, assets](Entity e, WorldMatrix& world, ModelRenderer& renderer) {
                m_pick->setMat4("uModel", world.value);
                m_pick->setInt("uEntityId", static_cast<int>(e.index()));
                if (ModelAsset* model = assets->get<ModelAsset>(renderer.handle)) {
                    for (const ModelAsset::Submesh& submesh : model->submeshes) {
                        submesh.mesh.draw();
                    }
                } else if (m_missingMesh) {
                    m_missingMesh->draw();
                }
            });
    }

    Scene& pickScene = runtime.scene();
    pickScene.each<WorldMatrix, gl::Mesh>(
        [this, &pickScene](Entity e, WorldMatrix& world, gl::Mesh& mesh) {
            // Pick truth must match visual truth: a bare mesh with no material never
            // reaches a colour pass, so it must not steal clicks while invisible.
            if (!pickScene.has<Material>(e) && !pickScene.has<PbrMaterial>(e) &&
                !pickScene.has<MaterialComponent>(e)) {
                return;
            }
            m_pick->setMat4("uModel", world.value);
            m_pick->setInt("uEntityId", static_cast<int>(e.index()));
            mesh.draw();
        });

    // Terrain occludes and is selectable like anything else it draws over; the cached
    // meshes are warm from the colour pass, so a missing cache entry just skips.
    if (const TerrainRenderData* terrain = runtime.tryResource<TerrainRenderData>()) {
        for (const TerrainDraw& draw : terrain->draws) {
            const WorldMatrix* world = pickScene.tryGet<WorldMatrix>(draw.entity);
            const auto cached = m_terrainMeshes.find(draw.entity);
            if (world == nullptr || cached == m_terrainMeshes.end() ||
                !cached->second.mesh.has_value()) {
                continue;
            }
            m_pick->setMat4("uModel", world->value);
            m_pick->setInt("uEntityId", static_cast<int>(draw.entity.index()));
            cached->second.mesh->draw();
        }
    }

    // y arrives measured from the top; flip to GL's bottom-left origin for the read.
    const std::int32_t id = m_pickBuffer->readPixel(x, height - 1 - y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (pickWasBlend == GL_TRUE) {
        glEnable(GL_BLEND);
    }

    if (id < 0) {
        return Entity{};
    }
    for (const Entity e : runtime.scene().entities()) {
        if (e.index() == static_cast<Entity::Index>(id)) {
            return e;
        }
    }
    return Entity{};
}

} // namespace rb
