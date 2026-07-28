#include "rabbet/scene/CameraShake.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace rb {

void applyCameraShake(RenderView& view, const CameraShake& shake) {
    if (shake.remaining <= 0.0f || shake.duration <= 0.0f || shake.amplitude <= 0.0f) {
        return;
    }
    const float falloff = shake.remaining / shake.duration;
    const float strength = shake.amplitude * falloff * falloff;
    // Fixed mixed-frequency sine pairs read as noise but stay deterministic, so a
    // headless test can pin the arithmetic (math.random would desync it).
    const float t = shake.time;
    const glm::vec3 offset(std::sin(t * 31.0f) * 0.6f, std::sin(t * 47.0f + 1.7f) * 0.45f,
                           0.0f);
    const float roll = std::sin(t * 23.0f + 0.9f) * 0.015f * strength;
    // A view-space translate shifts the whole world in camera coordinates; the tiny
    // roll sells the impact without steering where the player is looking.
    view.view = glm::rotate(glm::mat4(1.0f), roll, glm::vec3(0.0f, 0.0f, 1.0f)) *
                glm::translate(glm::mat4(1.0f), offset * strength) * view.view;
}

} // namespace rb
