#pragma once

#include <cstddef>
#include <vector>

#include <nlohmann/json.hpp>

#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;

// One tolerance for positional parent refs, shared by the scene and prefab loaders: a
// missing ref is a root, a non-unsigned / out-of-range / self ref warns and leaves the
// record a root, and a link the hierarchy gate refuses (a cycle) warns the same way
// instead of corrupting state. `where` and `noun` keep each loader's log voice
// ("scene load: entity 3 ..." / "prefab: record 3 ...").
void linkParentRef(Scene& scene, const nlohmann::json& record,
                   const std::vector<Entity>& created, std::size_t index, const char* where,
                   const char* noun);

} // namespace rb
