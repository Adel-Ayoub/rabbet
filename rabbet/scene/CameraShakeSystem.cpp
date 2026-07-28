#include "rabbet/scene/CameraShakeSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/scene/CameraShake.h"

#include <algorithm>

namespace rb {

void CameraShakeSystem::onPlayBegin(Runtime& runtime) {
    if (!runtime.hasResource<CameraShake>()) {
        runtime.addResource<CameraShake>();
    }
    runtime.resource<CameraShake>() = CameraShake{};
}

void CameraShakeSystem::onUpdate(Runtime& runtime, float dt) {
    if (!runtime.hasResource<CameraShake>()) {
        return;
    }
    CameraShake& shake = runtime.resource<CameraShake>();
    if (shake.remaining > 0.0f) {
        shake.time += dt;
        shake.remaining = std::max(0.0f, shake.remaining - dt);
    } else {
        // Idle phase resets, so every shake starts the same and a late-session one
        // never lands on a degraded float accumulator.
        shake.time = 0.0f;
    }
}

void CameraShakeSystem::onPlayEnd(Runtime& runtime) {
    // Stop restores the editor view; a shake must not survive into the next session.
    if (runtime.hasResource<CameraShake>()) {
        runtime.resource<CameraShake>() = CameraShake{};
    }
}

} // namespace rb
