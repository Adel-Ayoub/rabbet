#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/BuiltinShaders.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderUniform.h"
#include "rabbet/render/Shadow.h"
#include "rabbet/render/Viewport.h"
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
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rb {
namespace {

constexpr std::size_t kMaxDirectionalLights = 4;
constexpr std::size_t kMaxPointLights = 8;
constexpr int kShadowMapSize = 2048;
constexpr unsigned int kShadowTextureUnit = 1;

constexpr const char* kDepthVertex = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightSpace;
void main() {
    gl_Position = uLightSpace * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kDepthFragment = R"(#version 410 core
void main() {}
)";

constexpr const char* kPickVertex = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uViewProjection;
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kPickFragment = R"(#version 410 core
uniform int uEntityId;
layout(location = 0) out int oEntityId;
void main() {
    oEntityId = uEntityId;
}
)";

constexpr const char* kFlatVertex = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uViewProjection;
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFlatFragment = R"(#version 410 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

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
            asset->texture.bind(unit);
            program.setInt(texture.name, static_cast<int>(unit));
            ++unit;
        }
    }
}

void uploadLights(gl::Shader& shader, const glm::mat4& viewProjection, const glm::vec3& viewPosition,
                  const Lighting* lighting) {
    shader.setMat4("uViewProjection", viewProjection);
    shader.setVec3("uViewPosition", viewPosition);
    if (lighting == nullptr) {
        shader.setVec3("uAmbient", glm::vec3(0.1f));
        shader.setInt("uDirectionalCount", 0);
        shader.setInt("uPointCount", 0);
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
    m_depth = gl::Shader::fromSource(kDepthVertex, kDepthFragment);
    if (!m_depth) {
        log::error("render system: failed to build the depth shader");
    }
    m_pick = gl::Shader::fromSource(kPickVertex, kPickFragment);
    if (!m_pick) {
        log::error("render system: failed to build the pick shader");
    }
    m_flat = gl::Shader::fromSource(kFlatVertex, kFlatFragment);
    if (!m_flat) {
        log::error("render system: failed to build the flat shader");
    }
    m_shadowMap = gl::DepthMap::create(kShadowMapSize, kShadowMapSize);
    m_missingMesh = gl::Mesh::create(geometry::cube());
    m_missingTexture = gl::Texture::solid(255, 0, 255);
    m_primitiveCube = gl::Mesh::create(geometry::cube());
    m_primitiveSphere = gl::Mesh::create(geometry::sphere());
    m_primitivePlane = gl::Mesh::create(geometry::quad());
    m_whiteTexture = gl::Texture::solid(255, 255, 255);
}

void RenderSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<RenderView>()) {
        return;
    }
    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;
    const Lighting* lighting = runtime.tryResource<Lighting>();

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

    if (m_phong && runtime.scene().count<Material>() > 0) {
        m_phong->bind();
        uploadLights(*m_phong, viewProjection, view.position, lighting);
        m_phong->setInt("uTexture", 0);
        m_phong->setInt("uShadowMap", static_cast<int>(kShadowTextureUnit));
        m_phong->setInt("uHasShadowMap", hasShadow);
        m_phong->setMat4("uLightSpace", lightSpace);
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
        uploadLights(*m_pbr, viewProjection, view.position, lighting);
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
                m_pbr->setFloat("uMetallic", material.metallic);
                m_pbr->setFloat("uRoughness", material.roughness);
                m_pbr->setFloat("uAo", material.ao);
                material.albedo.bind(0);
                mesh.draw();
            });
    }

    if (m_pbr && assets != nullptr && runtime.scene().count<ModelRenderer>() > 0) {
        m_pbr->bind();
        uploadLights(*m_pbr, viewProjection, view.position, lighting);
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
        uploadLights(*m_pbr, viewProjection, view.position, lighting);
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
                uploadLights(*program, viewProjection, view.position, lighting);
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
                    program->setFloat("uMetallic", 0.0f);
                    program->setFloat("uRoughness", 0.8f);
                    program->setFloat("uAo", 1.0f);
                    applyMaterialOverrides(*program, *material, *assets);
                    mesh->draw();
                }
            });
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
        runtime.scene().each<Transform, BoxCollider>(
            [this](Entity, Transform& transform, BoxCollider& box) {
                if (!m_primitiveCube) {
                    return;
                }
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position) *
                                        glm::mat4_cast(transform.rotation) *
                                        glm::translate(glm::mat4(1.0f), box.offset) *
                                        glm::scale(glm::mat4(1.0f), box.halfExtents * 2.0f);
                m_flat->setMat4("uModel", model);
                m_primitiveCube->draw();
            });
        runtime.scene().each<Transform, SphereCollider>(
            [this](Entity, Transform& transform, SphereCollider& sphere) {
                if (!m_primitiveSphere) {
                    return;
                }
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position) *
                                        glm::mat4_cast(transform.rotation) *
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

    runtime.scene().each<WorldMatrix, gl::Mesh>(
        [this](Entity e, WorldMatrix& world, gl::Mesh& mesh) {
            m_pick->setMat4("uModel", world.value);
            m_pick->setInt("uEntityId", static_cast<int>(e.index()));
            mesh.draw();
        });

    // y arrives measured from the top; flip to GL's bottom-left origin for the read.
    const std::int32_t id = m_pickBuffer->readPixel(x, height - 1 - y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

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
