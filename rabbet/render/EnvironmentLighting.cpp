#include "rabbet/render/EnvironmentLighting.h"

#include "rabbet/render/Geometry.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <optional>
#include <utility>

namespace rb {
namespace {

constexpr const char* kConvolveVertex = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3 vDir;
void main() {
    vDir = aPosition;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

// Cosine-weighted hemisphere integral around the fragment direction: the diffuse irradiance a
// surface with that normal receives from the environment. `up` flips near the poles to avoid a
// degenerate tangent basis.
constexpr const char* kConvolveFragment = R"(#version 410 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uEnvironment;
const float PI = 3.14159265359;
void main() {
    vec3 N = normalize(vDir);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    const float delta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta) {
            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDir = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += texture(uEnvironment, sampleDir).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }
    FragColor = vec4(PI * irradiance / samples, 1.0);
}
)";

} // namespace

EnvironmentLight buildEnvironmentLight(const gl::Cubemap& environment, int size) {
    gl::Cubemap irradiance = gl::Cubemap::empty(size);
    std::optional<gl::Shader> shader = gl::Shader::fromSource(kConvolveVertex, kConvolveFragment);
    if (!shader) {
        log::error("environment lighting: failed to build the irradiance convolution shader");
        return EnvironmentLight{std::move(irradiance), 1.0f, false};
    }
    gl::Mesh cube = gl::Mesh::create(geometry::cube());

    GLint prevFramebuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevSeamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // integrate the source across face edges, no seams

    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::vec3 origin{0.0f};
    const glm::mat4 views[6] = {
        glm::lookAt(origin, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
    };

    shader->bind();
    shader->setInt("uEnvironment", 0);
    shader->setMat4("uProjection", projection);
    environment.bind(0);
    glViewport(0, 0, size, size);
    for (unsigned int face = 0; face < 6u; ++face) {
        shader->setMat4("uView", views[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, irradiance.id(), 0);
        glClear(GL_COLOR_BUFFER_BIT);
        cube.draw();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    prevDepth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    prevCull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    prevSeamless ? glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)
                 : glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glDeleteFramebuffers(1, &fbo);

    return EnvironmentLight{std::move(irradiance), 1.0f, false};
}

} // namespace rb
