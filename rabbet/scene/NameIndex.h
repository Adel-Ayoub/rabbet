#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;

// First-match Name lookups without the per-entity scan world.find used to pay per call.
// rebuild() walks the Name pool once in its iteration order and keeps the first entity
// per value, so find() answers exactly what that scan would have answered at rebuild
// time. The ECS publishes no change events, so the owner rebuilds once per tick; within
// a tick the pool is stable (spawn/destroy are deferred and scripts cannot rename).
class NameIndex {
public:
    void rebuild(Scene& scene);
    void clear();

    // The first entity whose Name equals `name` in pool order, or kNullEntity.
    [[nodiscard]] Entity find(std::string_view name) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_first.size(); }

private:
    struct Hash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    std::unordered_map<std::string, Entity, Hash, std::equal_to<>> m_first;
};

} // namespace rb
