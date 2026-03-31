#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rabbet/render/RenderView.h"

namespace rb {
class Input;
} // namespace rb

namespace rb::editor {

// A free-look editor camera, adapted from examples/common/FlyCamera. It produces
// the RenderView the editor viewport renders with. Mouse-look and WASD/QE movement
// (with momentum and a Shift boost) apply only while `active` — the editor passes
// active = viewport hovered && look-button held.
class EditorCamera {
public:
    void update(const rb::Input& input, float dt, bool active);
    [[nodiscard]] rb::RenderView renderView(float aspect) const;
    [[nodiscard]] glm::vec3 position() const noexcept { return m_position; }
    void reset();

private:
    [[nodiscard]] glm::quat orientation() const;

    glm::vec3 m_position{0.0f, 0.5f, 4.0f};
    float m_yaw = 0.0f;
    float m_pitch = -0.07f;
    glm::vec3 m_velocity{0.0f};
    float m_speed = 8.0f;
    float m_acceleration = 40.0f;
    float m_damping = 9.0f;
    float m_sensitivity = 0.0022f;
    float m_boost = 3.0f;
    float m_fovY = glm::radians(60.0f);
    float m_near = 0.05f;
    float m_far = 500.0f;
    bool m_primed = false;
};

} // namespace rb::editor
