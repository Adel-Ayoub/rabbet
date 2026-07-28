#include "rabbet/scene/CameraSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/scene/CameraShake.h"
#include "rabbet/scene/CameraView.h"

#include <optional>

namespace rb {

void CameraSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<RenderView>()) {
        runtime.addResource<RenderView>();
    }

    float aspect = 1.0f;
    if (runtime.hasResource<Viewport>()) {
        aspect = runtime.resource<Viewport>().aspect();
    }

    if (const std::optional<RenderView> view = activeCameraView(runtime.scene(), aspect)) {
        runtime.resource<RenderView>() = *view;
        if (CameraShake* shake = runtime.tryResource<CameraShake>()) {
            applyCameraShake(runtime.resource<RenderView>(), *shake);
        }
    }
}

} // namespace rb
