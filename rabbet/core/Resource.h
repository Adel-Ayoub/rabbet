#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "rabbet/core/TypeId.h"

namespace rb {

template <typename T>
concept Resource = std::is_object_v<T> && !std::is_const_v<T>;

class ResourceRegistry {
public:
    template <Resource T, typename... Args>
    T& add(Args&&... args) {
        const std::size_t id = slot<T>();
        if (id >= m_slots.size()) {
            m_slots.resize(id + 1);
        }
        auto holder = std::make_unique<Holder<T>>(std::forward<Args>(args)...);
        T& ref = holder->value;
        m_slots[id] = std::move(holder);
        return ref;
    }

    template <Resource T>
    [[nodiscard]] T* tryGet() noexcept {
        const std::size_t id = slot<T>();
        if (id >= m_slots.size() || !m_slots[id]) {
            return nullptr;
        }
        return &static_cast<Holder<T>*>(m_slots[id].get())->value;
    }

    template <Resource T>
    [[nodiscard]] const T* tryGet() const noexcept {
        const std::size_t id = slot<T>();
        if (id >= m_slots.size() || !m_slots[id]) {
            return nullptr;
        }
        return &static_cast<const Holder<T>*>(m_slots[id].get())->value;
    }

    template <Resource T>
    [[nodiscard]] bool has() const noexcept {
        return tryGet<T>() != nullptr;
    }

    template <Resource T>
    [[nodiscard]] T& get() noexcept {
        T* ptr = tryGet<T>();
        assert(ptr != nullptr && "resource has not been registered");
        return *ptr;
    }

private:
    struct HolderBase {
        HolderBase() = default;
        HolderBase(const HolderBase&) = delete;
        HolderBase& operator=(const HolderBase&) = delete;
        virtual ~HolderBase() = default;
    };

    template <typename T>
    struct Holder final : HolderBase {
        template <typename... Args>
        explicit Holder(Args&&... args) : value(std::forward<Args>(args)...) {}
        T value;
    };

    struct ResourceFamily {};

    template <typename T>
    [[nodiscard]] static std::size_t slot() noexcept {
        return detail::typeId<ResourceFamily, T>();
    }

    std::vector<std::unique_ptr<HolderBase>> m_slots;
};

} // namespace rb
