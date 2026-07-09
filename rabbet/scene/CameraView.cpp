#include "rabbet/scene/CameraView.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"

namespace rb {

std::optional<RenderView> activeCameraView(Scene& scene, float aspect) {
    std::optional<RenderView> result;
    scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform&) {
        if (result.has_value()) {
            return; // the first camera wins; a scene is expected to author one
        }
        // World pose through the parent chain: an ancestor's scale stretches the
        // camera's mounting offset but never enters the view basis, the same rule that
        // keeps the camera's own scale from warping the world.
        const WorldPose pose = worldPoseOf(scene, entity);
        RenderView view;
        const glm::mat4 world =
            glm::translate(glm::mat4(1.0f), pose.position) * glm::mat4_cast(pose.rotation);
        view.view = glm::inverse(world);
        view.projection = glm::perspective(camera.fovY, std::max(aspect, 1.0e-4f),
                                           camera.nearPlane, camera.farPlane);
        view.position = pose.position;
        result = view;
    });
    return result;
}

} // namespace rb
