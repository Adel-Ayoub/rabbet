#pragma once

#include "rabbet/render/gl/Shader.h"

#include <glm/glm.hpp>

#include <optional>

namespace rb::editor {

// Editor-only ground grid: an infinite y = 0 plane raycast per fragment, occluded by
// the scene through gl_FragDepth, self-contained like the thumbnail shader so the
// engine's own passes stay untouched.
class GridRenderer {
public:
    ~GridRenderer();
    void init();
    void draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos,
              bool hdrOutput);

private:
    std::optional<gl::Shader> m_shader;
    unsigned int m_vao = 0;
};

} // namespace rb::editor
