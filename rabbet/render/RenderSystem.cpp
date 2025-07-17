#include "rabbet/render/RenderSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

namespace rb {
namespace {

constexpr const char* kVertexSource = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentSource = R"(#version 410 core
in vec3 vNormal;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec3 uTint;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float intensity = 0.25 + 0.75 * diffuse;
    vec3 base = texture(uTexture, vUv).rgb * uTint;
    FragColor = vec4(base * intensity, 1.0);
}
)";

} // namespace

void RenderSystem::onStart(Runtime&) {
    m_shader = gl::Shader::fromSource(kVertexSource, kFragmentSource);
    if (!m_shader) {
        log::error("render system: failed to build the forward shader");
    }
}

void RenderSystem::onUpdate(Runtime& runtime, float) {
    if (!m_shader || !runtime.hasResource<RenderView>()) {
        return;
    }

    const RenderView& renderView = runtime.resource<RenderView>();
    const glm::mat4 viewProjection = renderView.projection * renderView.view;

    m_shader->bind();
    m_shader->setMat4("uViewProjection", viewProjection);
    m_shader->setInt("uTexture", 0);

    runtime.scene().each<WorldMatrix, gl::Mesh, Material>(
        [this](Entity, WorldMatrix& world, gl::Mesh& mesh, Material& material) {
            m_shader->setMat4("uModel", world.value);
            m_shader->setVec3("uTint", material.tint);
            material.texture.bind(0);
            mesh.draw();
        });
}

} // namespace rb
