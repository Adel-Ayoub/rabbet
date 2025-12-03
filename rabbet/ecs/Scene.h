#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <tuple>
#include <vector>

#include "rabbet/ecs/Component.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/ecs/Pool.h"

namespace rb {

class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    ~Scene() = default;

    [[nodiscard]] Entity create();
    void destroy(Entity e);
    [[nodiscard]] bool alive(Entity e) const noexcept;
    [[nodiscard]] std::size_t aliveCount() const noexcept { return m_aliveCount; }

    template <Component T, typename... Args>
    T& add(Entity e, Args&&... args) {
        assert(alive(e) && "add() called on a dead or invalid entity");
        return pool<T>().assign(e.index(), std::forward<Args>(args)...);
    }

    template <Component T>
    void remove(Entity e) {
        if (!alive(e)) {
            return;
        }
        if (Pool<T>* p = tryPool<T>()) {
            p->remove(e.index());
        }
    }

    template <Component T>
    [[nodiscard]] bool has(Entity e) const noexcept {
        if (!alive(e)) {
            return false;
        }
        const Pool<T>* p = tryPool<T>();
        return p != nullptr && p->contains(e.index());
    }

    template <Component T>
    [[nodiscard]] T& get(Entity e) {
        assert(has<T>(e) && "get() called on an entity without that component");
        return pool<T>().get(e.index());
    }

    template <Component T>
    [[nodiscard]] T* tryGet(Entity e) {
        if (!alive(e)) {
            return nullptr;
        }
        Pool<T>* p = tryPool<T>();
        return p != nullptr ? p->tryGet(e.index()) : nullptr;
    }

    template <Component T>
    [[nodiscard]] std::size_t count() const noexcept {
        const Pool<T>* p = tryPool<T>();
        return p != nullptr ? p->size() : 0;
    }

    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        static_assert(sizeof...(Ts) >= 1, "each<>() needs at least one component type");

        std::tuple<Pool<Ts>*...> pools{tryPool<Ts>()...};
        const bool anyMissing = (... || (std::get<Pool<Ts>*>(pools) == nullptr));
        if (anyMissing) {
            return;
        }

        if constexpr (sizeof...(Ts) == 1) {
            // One component: walk its dense arrays directly, skipping the per-entity
            // contains() test and the sparse get() indirection.
            Pool<Ts...>* pool = std::get<0>(pools);
            const std::span<const Entity::Index> owners = pool->entities();
            const std::span<Ts...> components = pool->components();
            for (std::size_t i = 0; i < components.size(); ++i) {
                const Entity::Index e = owners[i];
                fn(Entity{e, m_versions[e]}, components[i]);
            }
            return;
        }

        IPool* driver = nullptr;
        const auto consider = [&driver](IPool* candidate) {
            if (driver == nullptr || candidate->size() < driver->size()) {
                driver = candidate;
            }
        };
        (consider(std::get<Pool<Ts>*>(pools)), ...);

        for (const Entity::Index e : driver->entities()) {
            if ((std::get<Pool<Ts>*>(pools)->contains(e) && ...)) {
                fn(Entity{e, m_versions[e]}, std::get<Pool<Ts>*>(pools)->get(e)...);
            }
        }
    }

private:
    template <Component T>
    Pool<T>& pool() {
        const std::size_t id = detail::componentId<T>();
        if (id >= m_pools.size()) {
            m_pools.resize(id + 1);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<Pool<T>>();
        }
        return *static_cast<Pool<T>*>(m_pools[id].get());
    }

    template <Component T>
    Pool<T>* tryPool() {
        const std::size_t id = detail::componentId<T>();
        if (id >= m_pools.size() || !m_pools[id]) {
            return nullptr;
        }
        return static_cast<Pool<T>*>(m_pools[id].get());
    }

    template <Component T>
    const Pool<T>* tryPool() const {
        const std::size_t id = detail::componentId<T>();
        if (id >= m_pools.size() || !m_pools[id]) {
            return nullptr;
        }
        return static_cast<const Pool<T>*>(m_pools[id].get());
    }

    std::vector<Entity::Version> m_versions;
    std::vector<Entity::Index> m_freeIndices;
    std::vector<std::unique_ptr<IPool>> m_pools;
    std::size_t m_aliveCount = 0;
};

} // namespace rb
