#include "rabbet/scene/TransformSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/WorldMatrix.h"

namespace rb {

void TransformSystem::onUpdate(Runtime& runtime, float) {
    Scene& scene = runtime.scene();
    scene.each<Transform>([&scene](Entity entity, Transform& transform) {
        scene.add<WorldMatrix>(entity, WorldMatrix{transform.matrix()});
    });
}

} // namespace rb
