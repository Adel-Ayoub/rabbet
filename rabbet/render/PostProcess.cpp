#include "rabbet/render/PostProcess.h"

#include "rabbet/ecs/Scene.h"

namespace rb {

PostProcess* activePostProcess(Scene& scene) {
    PostProcess* found = nullptr;
    scene.each<PostProcess>([&found](Entity, PostProcess& post) {
        if (found == nullptr && post.enabled) {
            found = &post;
        }
    });
    return found;
}

} // namespace rb
