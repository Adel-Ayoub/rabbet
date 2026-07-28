#include "editor/GridRenderer.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>

namespace rb::editor {
namespace {

constexpr const char* kGridVert = R"(#version 410 core
uniform mat4 uInvViewProj;
out vec3 vNear;
out vec3 vFar;
void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 nearPoint = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farPoint = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vNear = nearPoint.xyz / nearPoint.w;
    vFar = farPoint.xyz / farPoint.w;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

constexpr const char* kGridFrag = R"(#version 410 core
in vec3 vNear;
in vec3 vFar;
out vec4 FragColor;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform int uHdrOutput; // 1: emit linear for the post pipeline; 0 (default): display values as-is

float gridLine(vec2 pos, float spacing) {
    vec2 cell = pos / spacing;
    vec2 dist = abs(fract(cell - 0.5) - 0.5) / fwidth(cell);
    return 1.0 - clamp(min(dist.x, dist.y), 0.0, 1.0);
}

void main() {
    float denom = vFar.y - vNear.y;
    if (abs(denom) < 1e-6) {
        discard;
    }
    float t = -vNear.y / denom;
    if (t <= 0.0 || t >= 1.0) {
        discard;
    }
    vec3 world = mix(vNear, vFar, t);

    vec4 clip = uViewProj * vec4(world, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);

    float minor = gridLine(world.xz, 1.0);
    float major = gridLine(world.xz, 10.0);

    vec3 color = vec3(0.42, 0.44, 0.47);
    float alpha = max(minor * 0.22, major * 0.45);

    // The world axes read as colored lines: x runs along z = 0, z along x = 0.
    if (abs(world.z) < fwidth(world.z)) {
        color = vec3(0.85, 0.35, 0.35);
        alpha = max(alpha, 0.8);
    }
    if (abs(world.x) < fwidth(world.x)) {
        color = vec3(0.35, 0.55, 0.9);
        alpha = max(alpha, 0.8);
    }

    float dist = length(world.xz - uCameraPos.xz);
    float fade = 1.0 - clamp((dist - 40.0) / 40.0, 0.0, 1.0);
    alpha *= fade;
    if (alpha <= 0.003) {
        discard;
    }
    if (uHdrOutput == 1) {
        color = pow(color, vec3(2.2)); // authored as display values; the post chain re-encodes
    }
    FragColor = vec4(color, alpha);
}
)";

} // namespace

GridRenderer::~GridRenderer() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
}

void GridRenderer::init() {
    m_shader = gl::Shader::fromSource(kGridVert, kGridFrag);
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
