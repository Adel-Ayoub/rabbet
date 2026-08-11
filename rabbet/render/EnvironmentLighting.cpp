#include "rabbet/render/EnvironmentLighting.h"

#include "rabbet/render/Geometry.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/render/shaders/GlShaderSources.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <optional>
#include <utility>

namespace rb {

EnvironmentLight buildEnvironmentLight(const gl::Cubemap& environment, int size) {
    gl::Cubemap irradiance = gl::Cubemap::empty(size);
    // The convolve fragment integrates a cosine weighted hemisphere around each
    // fragment's interpolated direction to get diffuse irradiance, and flips its
    // tangent basis up vector near the poles where the default up would degenerate.
    std::optional<gl::Shader> shader =
        gl::Shader::fromSource(shaders::kConvolveVertex, shaders::kConvolveFragment);
    if (!shader) {
        log::error("environment lighting: failed to build the irradiance convolution shader");
        return EnvironmentLight{std::move(irradiance), 1.0f, false};
    }
    gl::Mesh cube = gl::Mesh::create(geometry::cube());

    // This runs mid-frame whenever a scene swaps its sky, not just once at startup, so
    // every piece of state it touches has to go back exactly as it was. Read and draw
    // bindings are saved separately: restoring both through GL_FRAMEBUFFER would collapse
    // a split binding into one.
    GLint prevDrawFramebuffer = 0;
    GLint prevReadFramebuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevProgram = 0;
    GLint prevVertexArray = 0;
    GLint prevActiveTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevSeamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

    // The per-face clear below would otherwise inherit whatever scissor rectangle and
    // colour mask the caller left set, leaving parts of the probe unwritten.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

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

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(static_cast<GLuint>(prevProgram));
    glBindVertexArray(static_cast<GLuint>(prevVertexArray));
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    prevDepth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    prevCull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    prevSeamless ? glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)
                 : glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    prevScissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
    glDeleteFramebuffers(1, &fbo);

    return EnvironmentLight{std::move(irradiance), 1.0f, false};
}

} // namespace rb
