#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Shadow.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>

namespace rb {
namespace {

constexpr std::size_t kMaxDirectionalLights = 4;
constexpr std::size_t kMaxPointLights = 8;
constexpr int kShadowMapSize = 2048;
constexpr unsigned int kShadowTextureUnit = 1;

constexpr const char* kVertexSource = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUv;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)";

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

constexpr const char* kShadowFunctions = R"(
uniform mat4 uLightSpace;
uniform sampler2D uShadowMap;
uniform int uHasShadowMap;
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    if (uHasShadowMap == 0) return 0.0;
    vec4 lightClip = uLightSpace * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += proj.z - bias > depth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}
)";

constexpr const char* kLightUniforms = R"(
const int kMaxDir = 4;
const int kMaxPoint = 8;
uniform vec3 uViewPosition;
uniform vec3 uAmbient;
uniform int uDirectionalCount;
uniform vec3 uDirectionalDirection[kMaxDir];
uniform vec3 uDirectionalColor[kMaxDir];
uniform int uPointCount;
uniform vec3 uPointPosition[kMaxPoint];
uniform vec3 uPointColor[kMaxPoint];
uniform vec3 uPointAttenuation[kMaxPoint];
)";

const std::string kPhongFragment = std::string(R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec3 uTint;
uniform float uSpecularStrength;
uniform float uShininess;
)") + kLightUniforms + kShadowFunctions + R"(
vec3 shade(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 reflection = reflect(-L, N);
    float specular = pow(max(dot(V, reflection), 0.0), uShininess) * uSpecularStrength;
    return radiance * (diffuse * albedo + vec3(specular));
}
void main() {
    vec3 albedo = texture(uTexture, vUv).rgb * uTint;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 color = uAmbient * albedo;
    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        vec3 contribution = shade(L, uDirectionalColor[i], N, V, albedo);
        if (i == 0) contribution *= (1.0 - shadowFactor(vWorldPos, N, L));
        color += contribution;
    }
    for (int i = 0; i < uPointCount; ++i) {
        vec3 toLight = uPointPosition[i] - vWorldPos;
        float distance = length(toLight);
        vec3 a = uPointAttenuation[i];
        float attenuation = 1.0 / (a.x + a.y * distance + a.z * distance * distance);
        color += shade(toLight / max(distance, 0.0001), uPointColor[i] * attenuation, N, V, albedo);
    }
    FragColor = vec4(color, 1.0);
}
)";

const std::string kPbrFragment = std::string(R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uAlbedoTex;
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
const float PI = 3.14159265359;
)") + kLightUniforms + kShadowFunctions + R"(
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nh = max(dot(N, H), 0.0);
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float geometrySchlick(float nv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nv / (nv * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlick(max(dot(N, V), 0.0), roughness) *
           geometrySchlick(max(dot(N, L), 0.0), roughness);
}
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 brdf(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, vec3 f0) {
    vec3 H = normalize(V + L);
    float ndf = distributionGGX(N, H, uRoughness);
    float g = geometrySmith(N, V, L, uRoughness);
    vec3 f = fresnelSchlick(max(dot(H, V), 0.0), f0);
    vec3 specular = (ndf * g * f) /
                    (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kd = (vec3(1.0) - f) * (1.0 - uMetallic);
    return (kd * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}
void main() {
    vec3 albedo = texture(uAlbedoTex, vUv).rgb * uBaseColor;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 f0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 lo = vec3(0.0);
    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        vec3 contribution = brdf(L, uDirectionalColor[i], N, V, albedo, f0);
        if (i == 0) contribution *= (1.0 - shadowFactor(vWorldPos, N, L));
        lo += contribution;
    }
    for (int i = 0; i < uPointCount; ++i) {
        vec3 toLight = uPointPosition[i] - vWorldPos;
        float distance = length(toLight);
        vec3 a = uPointAttenuation[i];
        float attenuation = 1.0 / (a.x + a.y * distance + a.z * distance * distance);
        lo += brdf(toLight / max(distance, 0.0001), uPointColor[i] * attenuation, N, V, albedo, f0);
    }
    vec3 ambient = uAmbient * albedo * uAo;
    vec3 color = ambient + lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)";

glm::mat3 normalMatrix(const glm::mat4& model) {
    return glm::transpose(glm::inverse(glm::mat3(model)));
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

void RenderSystem::onStart(Runtime&) {
    m_phong = gl::Shader::fromSource(kVertexSource, kPhongFragment);
    if (!m_phong) {
        log::error("render system: failed to build the Phong shader");
    }
    m_pbr = gl::Shader::fromSource(kVertexSource, kPbrFragment);
    if (!m_pbr) {
        log::error("render system: failed to build the PBR shader");
    }
    m_depth = gl::Shader::fromSource(kDepthVertex, kDepthFragment);
    if (!m_depth) {
        log::error("render system: failed to build the depth shader");
    }
    m_shadowMap = gl::DepthMap::create(kShadowMapSize, kShadowMapSize);
    m_missingMesh = gl::Mesh::create(geometry::cube());
    m_missingTexture = gl::Texture::solid(255, 0, 255);
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

    const bool shadows = m_depth.has_value() && m_shadowMap.has_value() && lighting != nullptr &&
                         !lighting->directionalDirections.empty();
    glm::mat4 lightSpace{1.0f};

    if (shadows) {
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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (const Viewport* viewport = runtime.tryResource<Viewport>()) {
            glViewport(0, 0, viewport->width, viewport->height);
        }
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
            [this](Entity, WorldMatrix& world, gl::Mesh& mesh, Material& material) {
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
            [this](Entity, WorldMatrix& world, gl::Mesh& mesh, PbrMaterial& material) {
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
            [this, assets](Entity, WorldMatrix& world, ModelRenderer& renderer) {
                m_pbr->setMat4("uModel", world.value);
                m_pbr->setMat3("uNormalMatrix", normalMatrix(world.value));
                ModelAsset* model = assets->get<ModelAsset>(renderer.handle);
                if (model == nullptr) {
                    // unresolved or missing model: draw a loud magenta placeholder, but
                    // only when both fallback resources exist (else skip this entity).
                    if (m_missingMesh && m_missingTexture) {
                        m_pbr->setVec3("uBaseColor", glm::vec3(1.0f, 0.0f, 1.0f));
                        m_pbr->setFloat("uMetallic", 0.0f);
                        m_pbr->setFloat("uRoughness", 1.0f);
                        m_pbr->setFloat("uAo", 1.0f);
                        m_missingTexture->bind(0);
                        m_missingMesh->draw();
                    }
                    return;
                }
                for (const ModelAsset::Submesh& submesh : model->submeshes) {
                    m_pbr->setVec3("uBaseColor", submesh.baseColor);
                    m_pbr->setFloat("uMetallic", submesh.metallic);
                    m_pbr->setFloat("uRoughness", submesh.roughness);
                    m_pbr->setFloat("uAo", submesh.ao);
                    if (TextureAsset* texture = assets->get<TextureAsset>(submesh.albedo)) {
                        texture->texture.bind(0);
                    } else if (m_missingTexture) {
                        m_missingTexture->bind(0);
                    }
                    submesh.mesh.draw();
                }
            });
    }
}

} // namespace rb
