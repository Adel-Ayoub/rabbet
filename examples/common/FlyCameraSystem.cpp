#include "examples/common/FlyCameraSystem.h"

#include "examples/common/FlyCamera.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/platform/Input.h"
#include "rabbet/scene/Transform.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace ex {

void FlyCameraSystem::onUpdate(rb::Runtime& runtime, float dt) {
    const rb::Input* input = runtime.tryResource<rb::Input>();
    if (input == nullptr || dt <= 0.0f) {
        return;
    }

    runtime.scene().each<FlyCamera, rb::Transform>(
        [&](rb::Entity, FlyCamera& cam, rb::Transform& transform) {
            if (cam.primed) {
                cam.yaw -= static_cast<float>(input->cursorDeltaX()) * cam.sensitivity;
                cam.pitch -= static_cast<float>(input->cursorDeltaY()) * cam.sensitivity;
            } else {
                cam.primed = true;
            }
            const float limit = glm::radians(88.0f);
            cam.pitch = std::clamp(cam.pitch, -limit, limit);

            const glm::quat orientation = glm::angleAxis(cam.yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                          glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

            glm::vec3 wish{0.0f};
            if (input->keyDown(rb::Key::W)) wish += forward;
            if (input->keyDown(rb::Key::S)) wish -= forward;
            if (input->keyDown(rb::Key::D)) wish += right;
            if (input->keyDown(rb::Key::A)) wish -= right;
            if (input->keyDown(rb::Key::Space) || input->keyDown(rb::Key::E)) wish += worldUp;
            if (input->keyDown(rb::Key::LeftControl) || input->keyDown(rb::Key::Q)) wish -= worldUp;
            if (glm::dot(wish, wish) > 0.0f) {
                wish = glm::normalize(wish);
            }

            const float reach = input->keyDown(rb::Key::LeftShift) ? cam.boost : 1.0f;
            cam.velocity += wish * cam.acceleration * reach * dt;
            cam.velocity -= cam.velocity * glm::min(cam.damping * dt, 1.0f);
            const float topSpeed = cam.speed * reach;
            const float speed = glm::length(cam.velocity);
            if (speed > topSpeed) {
                cam.velocity *= topSpeed / speed;
            }

            transform.position += cam.velocity * dt;
            transform.rotation = orientation;
        });
}

} // namespace ex
