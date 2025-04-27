#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "rabbet/ecs/Component.h"

namespace rb {

class IPool {
public:
    using Index = std::uint32_t;

    IPool() = default;
    IPool(const IPool&) = delete;
    IPool& operator=(const IPool&) = delete;
    IPool(IPool&&) = delete;
    IPool& operator=(IPool&&) = delete;
    virtual ~IPool() = default;

    virtual void remove(Index entity) = 0;
    [[nodiscard]] virtual bool contains(Index entity) const = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual std::span<const Index> entities() const = 0;
};

template <Component T>
class Pool final : public IPool {
public:
    static constexpr Index kEmpty = ~Index{0};

    [[nodiscard]] bool contains(Index e) const override {
        return e < m_sparse.size() && m_sparse[e] != kEmpty;
    }

    template <typename... Args>
    T& assign(Index e, Args&&... args) {
        if (contains(e)) {
            T& existing = m_dense[m_sparse[e]];
            existing = T(std::forward<Args>(args)...);
            return existing;
        }
        if (e >= m_sparse.size()) {
            m_sparse.resize(e + 1, kEmpty);
        }
        m_sparse[e] = static_cast<Index>(m_dense.size());
        m_owners.push_back(e);
        return m_dense.emplace_back(std::forward<Args>(args)...);
    }

    void remove(Index e) override {
        if (!contains(e)) {
            return;
        }
        const Index slot = m_sparse[e];
        const Index last = static_cast<Index>(m_dense.size() - 1);
        if (slot != last) {
            m_dense[slot] = std::move(m_dense[last]);
            m_owners[slot] = m_owners[last];
            m_sparse[m_owners[last]] = slot;
        }
        m_dense.pop_back();
        m_owners.pop_back();
        m_sparse[e] = kEmpty;
    }

    [[nodiscard]] T& get(Index e) { return m_dense[m_sparse[e]]; }
    [[nodiscard]] const T& get(Index e) const { return m_dense[m_sparse[e]]; }

    [[nodiscard]] T* tryGet(Index e) { return contains(e) ? &m_dense[m_sparse[e]] : nullptr; }
    [[nodiscard]] const T* tryGet(Index e) const {
        return contains(e) ? &m_dense[m_sparse[e]] : nullptr;
    }

    [[nodiscard]] std::size_t size() const override { return m_dense.size(); }
    [[nodiscard]] bool empty() const { return m_dense.empty(); }

    [[nodiscard]] std::span<const Index> entities() const override { return m_owners; }
    [[nodiscard]] std::span<T> components() { return m_dense; }
    [[nodiscard]] std::span<const T> components() const { return m_dense; }

private:
    std::vector<Index> m_sparse;  // entity index -> dense slot, or kEmpty
    std::vector<Index> m_owners;  // dense slot -> entity index
    std::vector<T> m_dense;       // dense slot -> component, parallel to m_owners
};

} // namespace rb
