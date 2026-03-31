#include "editor/EditorCamera.h"

#include "rabbet/platform/Input.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace rb::editor {

glm::quat EditorCamera::orientation() const {
    return glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

void EditorCamera::update(const rb::Input& input, float dt, bool active) {
    if (dt <= 0.0f) {
        return;
    }

    if (active) {
        if (m_primed) {
            m_yaw -= static_cast<float>(input.cursorDeltaX()) * m_sensitivity;
            m_pitch -= static_cast<float>(input.cursorDeltaY()) * m_sensitivity;
            const float limit = glm::radians(88.0f);
            m_pitch = std::clamp(m_pitch, -limit, limit);
        } else {
            // Skip the first frame's delta to avoid a jump when look mode engages.
            m_primed = true;
        }
    } else {
        m_primed = false;
    }

    const glm::quat o = orientation();
    const glm::vec3 forward = o * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = o * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    glm::vec3 wish{0.0f};
    if (active) {
        if (input.keyDown(rb::Key::W)) wish += forward;
        if (input.keyDown(rb::Key::S)) wish -= forward;
        if (input.keyDown(rb::Key::D)) wish += right;
        if (input.keyDown(rb::Key::A)) wish -= right;
        if (input.keyDown(rb::Key::E) || input.keyDown(rb::Key::Space)) wish += worldUp;
        if (input.keyDown(rb::Key::Q) || input.keyDown(rb::Key::LeftControl)) wish -= worldUp;
    }
    if (glm::dot(wish, wish) > 0.0f) {
        wish = glm::normalize(wish);
    }

    const float reach = (active && input.keyDown(rb::Key::LeftShift)) ? m_boost : 1.0f;
    m_velocity += wish * m_acceleration * reach * dt;
    m_velocity -= m_velocity * glm::min(m_damping * dt, 1.0f);
    const float topSpeed = m_speed * reach;
    const float speed = glm::length(m_velocity);
    if (speed > topSpeed && speed > 0.0f) {
        m_velocity *= topSpeed / speed;
    }
    m_position += m_velocity * dt;
}

rb::RenderView EditorCamera::renderView(float aspect) const {
    rb::RenderView view;
    const glm::mat4 transform =
        glm::translate(glm::mat4(1.0f), m_position) * glm::mat4_cast(orientation());
    view.view = glm::inverse(transform);
    view.projection = glm::perspective(m_fovY, aspect > 0.0f ? aspect : 1.0f, m_near, m_far);
    view.position = m_position;
    return view;
}

void EditorCamera::reset() {
    m_position = glm::vec3(0.0f, 0.5f, 4.0f);
    m_yaw = 0.0f;
    m_pitch = -0.07f;
    m_velocity = glm::vec3(0.0f);
    m_primed = false;
}

} // namespace rb::editor
