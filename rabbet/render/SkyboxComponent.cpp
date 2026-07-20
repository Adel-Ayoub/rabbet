#include "rabbet/render/SkyboxComponent.h"

#include "rabbet/ecs/Scene.h"

namespace rb {

SkyboxComponent* activeSkybox(Scene& scene) {
    SkyboxComponent* found = nullptr;
    scene.each<SkyboxComponent>([&found](Entity, SkyboxComponent& sky) {
        if (found == nullptr) {
            found = &sky;
        }
    });
    return found;
}

} // namespace rb
