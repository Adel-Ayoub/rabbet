#include "rabbet/scene/CameraSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Transform.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace rb {

void CameraSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<RenderView>()) {
        runtime.addResource<RenderView>();
    }

    float aspect = 1.0f;
    if (runtime.hasResource<Viewport>()) {
        aspect = runtime.resource<Viewport>().aspect();
    }

    RenderView& renderView = runtime.resource<RenderView>();
    runtime.scene().each<Camera, Transform>(
        [&renderView, aspect](Entity, Camera& camera, Transform& transform) {
            renderView.view = glm::inverse(transform.matrix());
            renderView.projection =
                glm::perspective(camera.fovY, aspect, camera.nearPlane, camera.farPlane);
            renderView.position = transform.position;
        });
}

} // namespace rb
