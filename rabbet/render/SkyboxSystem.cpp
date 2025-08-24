#include "rabbet/render/SkyboxSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace rb {
namespace {

constexpr const char* kVertexSource = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProjection;
out vec3 vDir;
void main() {
    vDir = aPosition;
    vec4 clip = uViewProjection * vec4(aPosition, 1.0);
    gl_Position = clip.xyww;
}
)";

constexpr const char* kFragmentSource = R"(#version 410 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSkybox;
void main() {
    FragColor = texture(uSkybox, vDir);
}
)";

} // namespace

void SkyboxSystem::onStart(Runtime&) {
    m_shader = gl::Shader::fromSource(kVertexSource, kFragmentSource);
    if (!m_shader) {
        log::error("skybox system: failed to build the skybox shader");
    }
}

void SkyboxSystem::onUpdate(Runtime& runtime, float) {
    if (!m_shader || !runtime.hasResource<RenderView>() || !runtime.hasResource<Skybox>()) {
        return;
    }

    const RenderView& view = runtime.resource<RenderView>();
    const Skybox& skybox = runtime.resource<Skybox>();

    const glm::mat4 rotationOnlyView = glm::mat4(glm::mat3(view.view));
    const glm::mat4 viewProjection = view.projection * rotationOnlyView;

    glDepthFunc(GL_LEQUAL);
    m_shader->bind();
    m_shader->setMat4("uViewProjection", viewProjection);
    m_shader->setInt("uSkybox", 0);
    skybox.cubemap.bind(0);
    skybox.mesh.draw();
    glDepthFunc(GL_LESS);
}

} // namespace rb
