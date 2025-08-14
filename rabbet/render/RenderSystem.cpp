#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>

namespace rb {
namespace {

constexpr std::size_t kMaxDirectionalLights = 4;
constexpr std::size_t kMaxPointLights = 8;

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

constexpr const char* kPhongFragment = R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uTint;
uniform float uSpecularStrength;
uniform float uShininess;
uniform vec3 uViewPosition;
uniform vec3 uAmbient;

const int kMaxDir = 4;
const int kMaxPoint = 8;
uniform int uDirectionalCount;
uniform vec3 uDirectionalDirection[kMaxDir];
uniform vec3 uDirectionalColor[kMaxDir];
uniform int uPointCount;
uniform vec3 uPointPosition[kMaxPoint];
uniform vec3 uPointColor[kMaxPoint];
uniform vec3 uPointAttenuation[kMaxPoint];

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
        color += shade(normalize(-uDirectionalDirection[i]), uDirectionalColor[i], N, V, albedo);
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

constexpr const char* kPbrFragment = R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
uniform vec3 uViewPosition;
uniform vec3 uAmbient;

const int kMaxDir = 4;
const int kMaxPoint = 8;
const float PI = 3.14159265359;
uniform int uDirectionalCount;
uniform vec3 uDirectionalDirection[kMaxDir];
uniform vec3 uDirectionalColor[kMaxDir];
uniform int uPointCount;
uniform vec3 uPointPosition[kMaxPoint];
uniform vec3 uPointColor[kMaxPoint];
uniform vec3 uPointAttenuation[kMaxPoint];

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
    vec3 numerator = ndf * g * f;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    vec3 kd = (vec3(1.0) - f) * (1.0 - uMetallic);
    float nl = max(dot(N, L), 0.0);
    return (kd * albedo / PI + specular) * radiance * nl;
}

void main() {
    vec3 albedo = texture(uAlbedoTex, vUv).rgb * uBaseColor;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 f0 = mix(vec3(0.04), albedo, uMetallic);

    vec3 lo = vec3(0.0);
    for (int i = 0; i < uDirectionalCount; ++i) {
        lo += brdf(normalize(-uDirectionalDirection[i]), uDirectionalColor[i], N, V, albedo, f0);
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

void uploadFrame(gl::Shader& shader, const glm::mat4& viewProjection, const glm::vec3& viewPosition,
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
}

void RenderSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<RenderView>()) {
        return;
    }
    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;
    const Lighting* lighting = runtime.tryResource<Lighting>();

    if (m_phong) {
        m_phong->bind();
        uploadFrame(*m_phong, viewProjection, view.position, lighting);
        m_phong->setInt("uTexture", 0);
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

    if (m_pbr) {
        m_pbr->bind();
        uploadFrame(*m_pbr, viewProjection, view.position, lighting);
        m_pbr->setInt("uAlbedoTex", 0);
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
}

} // namespace rb
