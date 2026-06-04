#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rabbet/ecs/Scene.h"

namespace rb {

// Maps each component type to a stable name plus type-erased hooks. One registry
// drives scene save/load, entity duplication, and the editor inspector and
// add/remove-component menu — the single keystone for component reflection.
class ComponentRegistry {
public:
    // Editor draw hook. Deliberately takes only Scene+Entity so this header stays
    // free of any UI dependency; the editor supplies per-type ImGui lambdas through
    // setDrawer(), keeping ImGui out of the engine core (and the unit tests).
    using DrawFn = void (*)(Scene&, Entity);

    struct Entry {
        std::string name;
        bool (*has)(Scene&, Entity) = nullptr;
        void (*save)(Scene&, Entity, nlohmann::json&) = nullptr;
        void (*load)(Scene&, Entity, const nlohmann::json&) = nullptr;
        void (*addDefault)(Scene&, Entity) = nullptr;
        void (*remove)(Scene&, Entity) = nullptr;
        DrawFn drawInspector = nullptr;
    };

    template <Component T>
    void add(std::string name) {
        assert(find(name) == nullptr && "component type already registered under this name");
        Entry entry;
        entry.name = std::move(name);
        // The unary + forces conversion to a plain function pointer, which only
        // compiles for captureless lambdas — a compile-time guard on the hooks.
        entry.has = +[](Scene& scene, Entity e) { return scene.has<T>(e); };
        entry.save = +[](Scene& scene, Entity e, nlohmann::json& j) { j = scene.get<T>(e); };
        entry.load = +[](Scene& scene, Entity e, const nlohmann::json& j) {
            scene.add<T>(e, j.get<T>());
        };
        entry.addDefault = +[](Scene& scene, Entity e) {
            if (!scene.has<T>(e)) {
                scene.add<T>(e, T{});
            }
        };
        entry.remove = +[](Scene& scene, Entity e) { scene.remove<T>(e); };
        m_entries.push_back(std::move(entry));
    }

    // Attaches an editor draw hook to an already-registered component. No-op if the
    // name is unknown. Called from the editor only; the core never sets a drawer.
    void setDrawer(std::string_view name, DrawFn fn) noexcept;

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

    static ComponentRegistry& instance();

private:
    std::vector<Entry> m_entries;
};

} // namespace rb
