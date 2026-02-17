#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rabbet/ecs/Scene.h"

namespace rb {

// Maps each component type to a stable name plus type-erased hooks. One registry
// drives scene save/load (and, later, the editor inspector and add-component menu).
class ComponentRegistry {
public:
    struct Entry {
        std::string name;
        bool (*has)(Scene&, Entity) = nullptr;
        void (*save)(Scene&, Entity, nlohmann::json&) = nullptr;
        void (*load)(Scene&, Entity, const nlohmann::json&) = nullptr;
    };

    template <Component T>
    void add(std::string name) {
        Entry entry;
        entry.name = std::move(name);
        entry.has = [](Scene& scene, Entity e) { return scene.has<T>(e); };
        entry.save = [](Scene& scene, Entity e, nlohmann::json& j) { j = scene.get<T>(e); };
        entry.load = [](Scene& scene, Entity e, const nlohmann::json& j) {
            scene.add<T>(e, j.get<T>());
        };
        m_entries.push_back(std::move(entry));
    }

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

    static ComponentRegistry& instance();

private:
    std::vector<Entry> m_entries;
};

} // namespace rb
