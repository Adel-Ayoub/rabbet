#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Material.h"
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

constexpr const char* kFragmentSource = R"(#version 410 core
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

void main() {
    vec3 albedo = texture(uTexture, vUv).rgb * uTint;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 color = uAmbient * albedo;

    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        float diffuse = max(dot(N, L), 0.0);
        vec3 reflection = reflect(-L, N);
        float specular = pow(max(dot(V, reflection), 0.0), uShininess) * uSpecularStrength;
        color += uDirectionalColor[i] * (diffuse * albedo + vec3(specular));
    }

    for (int i = 0; i < uPointCount; ++i) {
        vec3 toLight = uPointPosition[i] - vWorldPos;
        float distance = length(toLight);
        vec3 L = toLight / max(distance, 0.0001);
        float diffuse = max(dot(N, L), 0.0);
        vec3 reflection = reflect(-L, N);
        float specular = pow(max(dot(V, reflection), 0.0), uShininess) * uSpecularStrength;
        vec3 a = uPointAttenuation[i];
        float attenuation = 1.0 / (a.x + a.y * distance + a.z * distance * distance);
        color += uPointColor[i] * (diffuse * albedo + vec3(specular)) * attenuation;
    }

    FragColor = vec4(color, 1.0);
}
)";

} // namespace

void RenderSystem::onStart(Runtime&) {
    m_shader = gl::Shader::fromSource(kVertexSource, kFragmentSource);
    if (!m_shader) {
        log::error("render system: failed to build the Phong shader");
    }
}

void RenderSystem::onUpdate(Runtime& runtime, float) {
    if (!m_shader || !runtime.hasResource<RenderView>()) {
        return;
    }

    const RenderView& view = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = view.projection * view.view;

    m_shader->bind();
    m_shader->setMat4("uViewProjection", viewProjection);
    m_shader->setVec3("uViewPosition", view.position);
    m_shader->setInt("uTexture", 0);

    if (const Lighting* lighting = runtime.tryResource<Lighting>()) {
        m_shader->setVec3("uAmbient", lighting->ambient);

        const auto dirCount =
            std::min(lighting->directionalDirections.size(), kMaxDirectionalLights);
        m_shader->setInt("uDirectionalCount", static_cast<int>(dirCount));
        m_shader->setVec3Array("uDirectionalDirection", {lighting->directionalDirections.data(), dirCount});
        m_shader->setVec3Array("uDirectionalColor", {lighting->directionalColors.data(), dirCount});

        const auto pointCount = std::min(lighting->pointPositions.size(), kMaxPointLights);
        m_shader->setInt("uPointCount", static_cast<int>(pointCount));
        m_shader->setVec3Array("uPointPosition", {lighting->pointPositions.data(), pointCount});
        m_shader->setVec3Array("uPointColor", {lighting->pointColors.data(), pointCount});
        m_shader->setVec3Array("uPointAttenuation", {lighting->pointAttenuations.data(), pointCount});
    } else {
        m_shader->setVec3("uAmbient", glm::vec3(0.1f));
        m_shader->setInt("uDirectionalCount", 0);
        m_shader->setInt("uPointCount", 0);
    }

    runtime.scene().each<WorldMatrix, gl::Mesh, Material>(
        [this](Entity, WorldMatrix& world, gl::Mesh& mesh, Material& material) {
            m_shader->setMat4("uModel", world.value);
            m_shader->setMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(world.value))));
            m_shader->setVec3("uTint", material.tint);
            m_shader->setFloat("uSpecularStrength", material.specular);
            m_shader->setFloat("uShininess", material.shininess);
            material.texture.bind(0);
            mesh.draw();
        });
}

} // namespace rb
