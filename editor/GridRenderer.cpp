#include "editor/GridRenderer.h"

#include "editor/shaders/GlShaderSources.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

namespace rb::editor {

GridRenderer::~GridRenderer() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
}

void GridRenderer::init() {
    m_shader = gl::Shader::fromSource(shaders::kGridVert, shaders::kGridFrag);
    if (!m_shader) {
        log::error("grid: failed to build the grid shader");
    }
    // Core profile refuses attributeless draws without a bound VAO.
    glGenVertexArrays(1, &m_vao);
}

void GridRenderer::draw(const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& cameraPos, bool hdrOutput) {
    if (!m_shader || m_vao == 0) {
        return;
    }
    const glm::mat4 viewProj = projection * view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);

    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevSrcRgb = 0;
    GLint prevDstRgb = 0;
    GLint prevSrcA = 0;
    GLint prevDstA = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    m_shader->bind();
    m_shader->setMat4("uInvViewProj", invViewProj);
    m_shader->setMat4("uViewProj", viewProj);
    m_shader->setVec3("uCameraPos", cameraPos);
    m_shader->setInt("uHdrOutput", hdrOutput ? 1 : 0);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glDepthMask(prevDepthMask);
    if (prevBlend == GL_FALSE) {
        glDisable(GL_BLEND);
    }
    glBlendFuncSeparate(static_cast<GLenum>(prevSrcRgb), static_cast<GLenum>(prevDstRgb),
                        static_cast<GLenum>(prevSrcA), static_cast<GLenum>(prevDstA));
    if (prevCull == GL_TRUE) {
        glEnable(GL_CULL_FACE);
    }
}

} // namespace rb::editor
