#include "rabbet/scene/CameraView.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Transform.h"

namespace rb {

std::optional<RenderView> activeCameraView(Scene& scene, float aspect) {
    std::optional<RenderView> result;
    scene.each<Camera, Transform>([&](Entity, Camera& camera, Transform& transform) {
        if (result.has_value()) {
            return; // the first camera wins; a scene is expected to author one
        }
        RenderView view;
        // Position + rotation only: a scaled camera entity must not scale the world.
        const glm::mat4 world = glm::translate(glm::mat4(1.0f), transform.position) *
                                glm::mat4_cast(transform.rotation);
        view.view = glm::inverse(world);
        view.projection = glm::perspective(camera.fovY, std::max(aspect, 1.0e-4f),
                                           camera.nearPlane, camera.farPlane);
        view.position = transform.position;
        result = view;
    });
    return result;
}

} // namespace rb
