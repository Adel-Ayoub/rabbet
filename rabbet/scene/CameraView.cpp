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
    scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform& transform) {
        if (result.has_value()) {
            return; // the first camera wins; a scene is expected to author one
        }
        // Compose the world pose up the parent chain. An ancestor's scale stretches the
        // camera's mounting offset but never enters the view basis, the same rule that
        // keeps the camera's own scale from warping the world.
        glm::vec3 position = transform.position;
        glm::quat rotation = transform.rotation;
        Entity walk = parentOf(scene, entity);
        for (int depth = 0; walk.valid() && depth < kMaxHierarchyDepth; ++depth) {
            if (const Transform* up = scene.tryGet<Transform>(walk)) {
                position = up->position + up->rotation * (up->scale * position);
                rotation = glm::normalize(up->rotation * rotation);
            }
            walk = parentOf(scene, walk);
        }
        RenderView view;
        const glm::mat4 world =
            glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
        view.view = glm::inverse(world);
        view.projection = glm::perspective(camera.fovY, std::max(aspect, 1.0e-4f),
                                           camera.nearPlane, camera.farPlane);
        view.position = position;
        result = view;
    });
    return result;
}

} // namespace rb
