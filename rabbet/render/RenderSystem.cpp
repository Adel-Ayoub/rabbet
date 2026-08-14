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
// Binding index of the RbPerFrame camera block, the GL face of the neutral contract's
// set 0 binding 0 slot.
constexpr unsigned int kCameraUniformBinding = 0;

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
    TextureConfig config;
    config.srgb = false;
    config.generateMipmaps = true;
    config.sampler.address = TextureAddress::ClampToEdge;
    return gl::Texture::fromPixels(pixels, kSize, kSize, 4, config);
}

bool usesMaterialAsset(Scene& scene, Entity entity) {
    const MaterialComponent* component = scene.tryGet<MaterialComponent>(entity);
    return component != nullptr && component->handle.valid();
}

} // namespace

struct RenderSystem::FrameContext {
    RenderSystem& renderer;
    Runtime& runtime;
    Scene& scene;
    const RenderView& view;
    AssetManager* assets;
    const Lighting* lighting;
    const EnvironmentLight* environment;
    glm::mat4 viewProjection;
    glm::mat4 lightSpace{1.0f};
    int hdrOutput{0};
    bool shadows{false};

    void uploadLighting(gl::Shader& program) const {
        uploadLights(program, viewProjection, view.position, lighting, environment, hdrOutput);
        program.setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        program.setInt("uHasShadowMap", shadows ? 1 : 0);
        program.setMat4("uLightSpace", lightSpace);
    }

    void drawModelSubmeshes(gl::Shader& program, const glm::mat4& world, ModelAsset* model,
                            const MaterialAsset* overrides) {
        program.setMat4("uModel", world);
        program.setMat3("uNormalMatrix", normalMatrix(world));
        if (model == nullptr) {
            if (renderer.m_missingMesh && renderer.m_missingTexture) {
                program.setVec3("uBaseColor", glm::vec3(1.0f, 0.0f, 1.0f));
                program.setVec3("uEmissive", glm::vec3(0.0f));
                program.setFloat("uMetallic", 0.0f);
                program.setFloat("uRoughness", 1.0f);
                program.setFloat("uAo", 1.0f);
                renderer.m_missingTexture->bind(0);
                if (overrides != nullptr && assets != nullptr) {
                    applyMaterialOverrides(program, *overrides, *assets);
                }
                renderer.m_missingMesh->draw();
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
                } else if (renderer.m_missingTexture) {
                    renderer.m_missingTexture->bind(0);
                }
            }
            if (overrides != nullptr && assets != nullptr) {
                applyMaterialOverrides(program, *overrides, *assets);
            }
            submesh.mesh.draw();
        }
    }
};

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
        compiled->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
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
    } else {
        m_phong->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
    }
    m_pbr = gl::Shader::fromSource(builtinLitVertexSource(), builtinPbrFragmentSource());
    if (!m_pbr) {
        log::error("render system: failed to build the PBR shader");
    } else {
        m_pbr->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
    }
    m_depth = gl::Shader::fromSource(shaders::kDepthVertex, shaders::kDepthFragment);
    if (!m_depth) {
        log::error("render system: failed to build the depth shader");
    } else {
        m_depth->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
    }
    m_pick = gl::Shader::fromSource(shaders::kPickVertex, shaders::kPickFragment);
    if (!m_pick) {
        log::error("render system: failed to build the pick shader");
    } else {
        m_pick->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
    }
    m_flat = gl::Shader::fromSource(shaders::kFlatVertex, shaders::kFlatFragment);
    if (!m_flat) {
        log::error("render system: failed to build the flat shader");
    } else {
        m_flat->bindUniformBlock("RbPerFrame", kCameraUniformBinding);
    }
    if (m_phong || m_pbr || m_depth || m_pick || m_flat) {
        gl::UniformBuffer cameraUbo = gl::UniformBuffer::create(sizeof(glm::mat4));
        if (cameraUbo.valid()) {
            m_cameraUbo = std::move(cameraUbo);
        }
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

void RenderSystem::drawShadowMap(FrameContext& frame) {
    if (!frame.shadows) {
        return;
    }

    // The caller may be drawing into an editor target instead of the default framebuffer.
    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    frame.lightSpace = directionalLightSpace(frame.lighting->directionalDirections.front(), 7.0f,
                                             1.0f, 30.0f, 14.0f);
    m_shadowMap->bindForWriting();
    glClear(GL_DEPTH_BUFFER_BIT);
    m_depth->bind();
    m_cameraUbo->upload(&frame.lightSpace, sizeof(frame.lightSpace));
    m_cameraUbo->bindBase(kCameraUniformBinding);
    frame.scene.each<WorldMatrix, gl::Mesh>([this](Entity, WorldMatrix& world, gl::Mesh& mesh) {
        m_depth->setMat4("uModel", world.value);
        mesh.draw();
    });
    if (frame.assets != nullptr) {
        frame.scene.each<WorldMatrix, ModelRenderer>(
            [this, &frame](Entity, WorldMatrix& world, ModelRenderer& renderer) {
                m_depth->setMat4("uModel", world.value);
                const ModelAsset* model = frame.assets->get<ModelAsset>(renderer.handle);
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
    frame.scene.each<WorldMatrix, Primitive>(
        [this](Entity, WorldMatrix& world, Primitive& primitive) {
            if (gl::Mesh* mesh = primitiveMesh(primitive.shape)) {
                m_depth->setMat4("uModel", world.value);
                mesh->draw();
            }
        });

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
               previousViewport[3]);
    m_shadowMap->bindTexture(kShadowTextureUnit);
    m_cameraUbo->upload(&frame.viewProjection, sizeof(frame.viewProjection));
    m_cameraUbo->bindBase(kCameraUniformBinding);
}

void RenderSystem::drawBuiltInMaterials(FrameContext& frame) {
    if (m_phong && frame.scene.count<Material>() > 0) {
        m_phong->bind();
        frame.uploadLighting(*m_phong);
        m_phong->setInt("uTexture", 0);
        m_phong->setVec3("uEmissive", glm::vec3(0.0f));
        frame.scene.each<WorldMatrix, gl::Mesh, Material>(
            [this, &frame](Entity entity, WorldMatrix& world, gl::Mesh& mesh,
                           Material& material) {
                if (usesMaterialAsset(frame.scene, entity)) {
                    return;
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

    if (m_pbr && frame.scene.count<PbrMaterial>() > 0) {
        m_pbr->bind();
        frame.uploadLighting(*m_pbr);
        m_pbr->setInt("uAlbedoTex", 0);
        frame.scene.each<WorldMatrix, gl::Mesh, PbrMaterial>(
            [this, &frame](Entity entity, WorldMatrix& world, gl::Mesh& mesh,
                           PbrMaterial& material) {
                if (usesMaterialAsset(frame.scene, entity)) {
                    return;
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

    if (m_pbr && frame.assets != nullptr && frame.scene.count<ModelRenderer>() > 0) {
        m_pbr->bind();
        frame.uploadLighting(*m_pbr);
        m_pbr->setInt("uAlbedoTex", 0);
        frame.scene.each<WorldMatrix, ModelRenderer>(
            [this, &frame](Entity entity, WorldMatrix& world, ModelRenderer& renderer) {
                if (usesMaterialAsset(frame.scene, entity)) {
                    return;
                }
                ModelAsset* model = frame.assets->get<ModelAsset>(renderer.handle);
                frame.drawModelSubmeshes(*m_pbr, world.value, model, nullptr);
            });
    }

    if (m_pbr && m_whiteTexture && frame.scene.count<Primitive>() > 0) {
        m_pbr->bind();
        frame.uploadLighting(*m_pbr);
        m_pbr->setInt("uAlbedoTex", 0);
        m_whiteTexture->bind(0);
        frame.scene.each<WorldMatrix, Primitive>(
            [this, &frame](Entity entity, WorldMatrix& world, Primitive& primitive) {
                if (usesMaterialAsset(frame.scene, entity)) {
                    return;
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
}

void RenderSystem::drawMaterialComponents(FrameContext& frame) {
    if (frame.assets == nullptr || frame.scene.count<MaterialComponent>() == 0) {
        return;
    }

    frame.scene.each<WorldMatrix, MaterialComponent>(
        [this, &frame](Entity entity, WorldMatrix& world, MaterialComponent& component) {
            if (!component.handle.valid()) {
                return;
            }
            MaterialAsset* material = frame.assets->get<MaterialAsset>(component.handle);
            if (material == nullptr) {
                return;
            }
            gl::Shader* program =
                shaderProgram(*frame.assets, material->shaderHandle, material->shader);
            if (program == nullptr) {
                program = m_pbr ? &*m_pbr : nullptr;
            }
            if (program == nullptr) {
                return;
            }

            program->bind();
            frame.uploadLighting(*program);
            program->setInt("uAlbedoTex", 0);
            program->setInt("uTexture", 0);

            if (ModelRenderer* renderer = frame.scene.tryGet<ModelRenderer>(entity)) {
                ModelAsset* model = frame.assets->get<ModelAsset>(renderer->handle);
                frame.drawModelSubmeshes(*program, world.value, model, material);
            } else if (Primitive* primitive = frame.scene.tryGet<Primitive>(entity)) {
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
                    applyMaterialOverrides(*program, *material, *frame.assets);
                    mesh->draw();
                }
            } else if (gl::Mesh* mesh = frame.scene.tryGet<gl::Mesh>(entity)) {
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
                applyMaterialOverrides(*program, *material, *frame.assets);
                mesh->draw();
            }
        });
}

void RenderSystem::drawTerrain(FrameContext& frame) {
    const TerrainRenderData* terrain = frame.runtime.tryResource<TerrainRenderData>();
    if (!m_terrain || !m_terrainFallback || terrain == nullptr || terrain->draws.empty()) {
        if (!m_terrainMeshes.empty()) {
            m_terrainMeshes.clear();
        }
        return;
    }

    // Texture units 0 and 1 hold albedo and shadow. Layers use 2 through 5,
    // the splat map uses 6 and environment lighting uses 7.
    constexpr unsigned int kLayerUnit0 = 2;
    constexpr unsigned int kSplatUnit = 6;
    const gl::Texture& fallback = *m_terrainFallback;

    m_terrain->bind();
    frame.uploadLighting(*m_terrain);
    m_terrain->setFloat("uMetallic", 0.0f);
    m_terrain->setFloat("uRoughness", 0.92f);
    for (int index = 0; index < 4; ++index) {
        m_terrain->setInt("uLayerAlbedo[" + std::to_string(index) + "]",
                          static_cast<int>(kLayerUnit0) + index);
    }
    m_terrain->setInt("uSplat", static_cast<int>(kSplatUnit));

    for (const TerrainDraw& draw : terrain->draws) {
        const WorldMatrix* world = frame.scene.tryGet<WorldMatrix>(draw.entity);
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

        for (int index = 0; index < 4; ++index) {
            const std::string suffix = "[" + std::to_string(index) + "]";
            const gl::Texture* texture = &fallback;
            glm::vec2 heightRange{0.0f, 1.0f};
            glm::vec2 slopeRange{0.0f, 1.0f};
            float tiling = 1.0f;
            float sharpness = 0.12f;
            if (index < draw.layerCount) {
                const TerrainLayerBinding& layer =
                    draw.layers[static_cast<std::size_t>(index)];
                if (frame.assets != nullptr && layer.albedo.valid()) {
                    if (TextureAsset* asset = frame.assets->get<TextureAsset>(layer.albedo)) {
                        texture = &asset->texture;
                    }
                }
                heightRange = layer.heightRange;
                slopeRange = layer.slopeRange;
                tiling = layer.tiling;
                sharpness = layer.sharpness;
            }
            texture->bind(kLayerUnit0 + static_cast<unsigned int>(index));
            m_terrain->setFloat("uLayerTiling" + suffix, tiling);
            m_terrain->setVec2("uLayerHeightRange" + suffix, heightRange);
            m_terrain->setVec2("uLayerSlopeRange" + suffix, slopeRange);
            m_terrain->setFloat("uLayerSharpness" + suffix, sharpness);
        }

        const gl::Texture* splat = &fallback;
        int hasSplat = 0;
        if (draw.blend == TerrainBlend::Splatmap && frame.assets != nullptr &&
            draw.splat.valid()) {
            if (TextureAsset* asset = frame.assets->get<TextureAsset>(draw.splat)) {
                splat = &asset->texture;
                hasSplat = 1;
            }
        }
        splat->bind(kSplatUnit);
        m_terrain->setInt("uHasSplat", hasSplat);
        cache.mesh->draw();
    }

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
}

void RenderSystem::drawWater(FrameContext& frame) {
    if (!m_water || !m_waterMesh || frame.scene.count<WaterComponent>() == 0) {
        return;
    }

    // Every shader octave repeats over this interval, so wrapping does not pop.
    constexpr double kWavePeriod = 8.0 * 3.14159265358979323846;
    constexpr unsigned int kWaterSkyUnit = 8;
    const Skybox* sky = frame.runtime.tryResource<Skybox>();
    bool passOpen = false;
    GLboolean wasBlend = GL_FALSE;
    GLboolean wasCull = GL_FALSE;
    GLboolean depthWrite = GL_TRUE;
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint activeUnit = GL_TEXTURE0;

    frame.scene.each<WaterComponent, WorldMatrix>(
        [&](Entity, WaterComponent& water, WorldMatrix& world) {
            if (!water.enabled) {
                return;
            }
            if (!passOpen) {
                // Disabled water must not change GL state.
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
                // Water remains visible from below.
                glDisable(GL_CULL_FACE);

                m_water->bind();
                uploadLights(*m_water, frame.viewProjection, frame.view.position, frame.lighting,
                             frame.environment, frame.hdrOutput);
                m_water->setInt("uSkybox", static_cast<int>(kWaterSkyUnit));
                m_water->setInt("uHasSkybox", sky != nullptr ? 1 : 0);
                // Keep the cubemap sampler complete when no skybox exists.
                if (sky != nullptr) {
                    sky->cubemap.bind(kWaterSkyUnit);
                } else if (m_fallbackCubemap) {
                    m_fallbackCubemap->bind(kWaterSkyUnit);
                }
            }

            WaterComponent safe = water;
            sanitizeWater(safe);
            const float phase = static_cast<float>(
                std::fmod(m_waterTime * static_cast<double>(safe.waveSpeed), kWavePeriod));
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

    if (!passOpen) {
        return;
    }
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

void RenderSystem::drawParticles(FrameContext& frame) {
    const ParticleRenderData* particles = frame.runtime.tryResource<ParticleRenderData>();
    if (!m_particle || !m_particleStream || !m_particleTexture || particles == nullptr ||
        particles->batches.empty()) {
        return;
    }

    const glm::mat4& view = frame.view.view;
    const glm::vec3 cameraRight{view[0][0], view[1][0], view[2][0]};
    const glm::vec3 cameraUp{view[0][1], view[1][1], view[2][1]};

    const GLboolean wasBlend = glIsEnabled(GL_BLEND);
    const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
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
    // Billboards are visible from either winding.
    glDisable(GL_CULL_FACE);

    m_particle->bind();
    m_particle->setMat4("uViewProjection", frame.viewProjection);
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

        const gl::Texture* sprite = &*m_particleTexture;
        if (frame.assets != nullptr && batch.sprite.valid()) {
            if (TextureAsset* texture = frame.assets->get<TextureAsset>(batch.sprite)) {
                sprite = &texture->texture;
            }
        }
        sprite->bind(0);

        m_particleVertices.clear();
        m_particleVertices.reserve(batch.particles.size() * 6);
        for (const ParticleBillboard& particle : batch.particles) {
            const glm::vec3 right = cameraRight * (particle.size * 0.5f);
            const glm::vec3 up = cameraUp * (particle.size * 0.5f);
            const glm::vec3 bottomLeft = particle.position - right - up;
            const glm::vec3 bottomRight = particle.position + right - up;
            const glm::vec3 topRight = particle.position + right + up;
            const glm::vec3 topLeft = particle.position - right + up;
            m_particleVertices.push_back({bottomLeft, {0.0f, 0.0f}, particle.color});
            m_particleVertices.push_back({bottomRight, {1.0f, 0.0f}, particle.color});
            m_particleVertices.push_back({topRight, {1.0f, 1.0f}, particle.color});
            m_particleVertices.push_back({bottomLeft, {0.0f, 0.0f}, particle.color});
            m_particleVertices.push_back({topRight, {1.0f, 1.0f}, particle.color});
            m_particleVertices.push_back({topLeft, {0.0f, 1.0f}, particle.color});
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

void RenderSystem::drawDebugColliders(FrameContext& frame) {
    const DebugDraw* debug = frame.runtime.tryResource<DebugDraw>();
    if (!m_flat || !m_cameraUbo || debug == nullptr || !debug->colliders) {
        return;
    }

    m_flat->bind();
    m_cameraUbo->bindBase(kCameraUniformBinding);
    m_flat->setVec3("uColor", glm::vec3(0.25f, 0.95f, 0.40f));
    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Use the composed world pose but keep collider dimensions in world space.
    frame.scene.each<Transform, BoxCollider>(
        [this, &frame](Entity entity, Transform&, BoxCollider& box) {
            if (!m_primitiveCube) {
                return;
            }
            const WorldPose pose = worldPoseOf(frame.scene, entity);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), pose.position) *
                                    glm::mat4_cast(pose.rotation) *
                                    glm::translate(glm::mat4(1.0f), box.offset) *
                                    glm::scale(glm::mat4(1.0f), box.halfExtents * 2.0f);
            m_flat->setMat4("uModel", model);
            m_primitiveCube->draw();
        });
    frame.scene.each<Transform, SphereCollider>(
        [this, &frame](Entity entity, Transform&, SphereCollider& sphere) {
            if (!m_primitiveSphere) {
                return;
            }
            const WorldPose pose = worldPoseOf(frame.scene, entity);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), pose.position) *
                                    glm::mat4_cast(pose.rotation) *
                                    glm::translate(glm::mat4(1.0f), sphere.offset) *
                                    glm::scale(glm::mat4(1.0f),
                                               glm::vec3(sphere.radius * 2.0f));
            m_flat->setMat4("uModel", model);
            m_primitiveSphere->draw();
        });

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_TEST);
}

void RenderSystem::onUpdate(Runtime& runtime, float dt) {
    // Long pauses from dialogs or scene loads must not jump water to a distant phase.
    m_waterTime += static_cast<double>(std::clamp(dt, 0.0f, 0.1f));
    if (!runtime.hasResource<RenderView>()) {
        return;
    }

    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;
    if (m_cameraUbo) {
        m_cameraUbo->upload(&viewProjection, sizeof(viewProjection));
        m_cameraUbo->bindBase(kCameraUniformBinding);
    }

    const Lighting* lighting = runtime.tryResource<Lighting>();
    const EnvironmentLight* environment = runtime.tryResource<EnvironmentLight>();

    // Directional lights keep authored order because they have no position to rank.
    Lighting selectedLights;
    if (lighting != nullptr && (lighting->pointPositions.size() > kMaxPointLights ||
                                lighting->spotPositions.size() > kMaxSpotLights)) {
        selectedLights = *lighting;
        if (selectedLights.pointPositions.size() > kMaxPointLights) {
            const std::vector<std::size_t> keep =
                selectNearestLights(selectedLights.pointPositions, view.position, kMaxPointLights);
            keepIndices(selectedLights.pointPositions, keep);
            keepIndices(selectedLights.pointColors, keep);
            keepIndices(selectedLights.pointAttenuations, keep);
        }
        if (selectedLights.spotPositions.size() > kMaxSpotLights) {
            const std::vector<std::size_t> keep =
                selectNearestLights(selectedLights.spotPositions, view.position, kMaxSpotLights);
            keepIndices(selectedLights.spotPositions, keep);
            keepIndices(selectedLights.spotDirections, keep);
            keepIndices(selectedLights.spotColors, keep);
            keepIndices(selectedLights.spotAttenuations, keep);
            keepIndices(selectedLights.spotCones, keep);
        }
        lighting = &selectedLights;
    }

    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets != nullptr) {
        // Resolve models when the caller did not install the asset resolve system.
        runtime.scene().each<ModelRenderer>([assets](Entity, ModelRenderer& renderer) {
            if (!renderer.handle.valid()) {
                renderer.handle = assets->find<ModelAsset>(renderer.model);
            }
        });
    }

    Scene& scene = runtime.scene();
    // A post-process pass owns tone mapping, while the direct path keeps it in the lit shader.
    const int hdrOutput = activePostProcess(scene) != nullptr ? 1 : 0;
    const bool shadows = m_depth.has_value() && m_shadowMap.has_value() &&
                         m_cameraUbo.has_value() && lighting != nullptr &&
                         !lighting->directionalDirections.empty();
    FrameContext frame{
        .renderer = *this,
        .runtime = runtime,
        .scene = scene,
        .view = view,
        .assets = assets,
        .lighting = lighting,
        .environment = environment,
        .viewProjection = viewProjection,
        .lightSpace = glm::mat4(1.0f),
        .hdrOutput = hdrOutput,
        .shadows = shadows,
    };

    drawShadowMap(frame);

    // Keep the sampler complete even when environment lighting is disabled.
    if (const gl::Cubemap* environmentCubemap =
            environment != nullptr ? &environment->irradiance
                                   : (m_fallbackCubemap ? &*m_fallbackCubemap : nullptr)) {
        environmentCubemap->bind(kEnvTextureUnit);
    }

    drawBuiltInMaterials(frame);
    drawMaterialComponents(frame);
    drawTerrain(frame);
    drawWater(frame);
    drawParticles(frame);
    drawDebugColliders(frame);
}

Entity RenderSystem::pick(Runtime& runtime, int x, int y) {
    if (!m_pick || !m_cameraUbo || !runtime.hasResource<RenderView>() ||
        !runtime.hasResource<Viewport>()) {
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
    m_cameraUbo->upload(&viewProjection, sizeof(viewProjection));
    m_cameraUbo->bindBase(kCameraUniformBinding);

    m_pickBuffer->bindAndClear();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    const GLboolean pickWasBlend = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND); // integer colour targets cannot be blended

    m_pick->bind();

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
