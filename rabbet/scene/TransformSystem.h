#pragma once

#include <unordered_map>

#include <glm/glm.hpp>

#include "rabbet/core/System.h"
#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;

class TransformSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;

private:
    glm::mat4 worldOf(Scene& scene, Entity entity, int depth);

    std::unordered_map<Entity, glm::mat4> m_worlds; // per-tick ancestor memo
    bool m_cycleWarned = false;
};

} // namespace rb
