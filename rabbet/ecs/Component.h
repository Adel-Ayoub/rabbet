#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace rb {

template <typename T>
concept Component = std::is_object_v<T> && std::movable<T> && !std::is_const_v<T>;

namespace detail {

inline std::size_t nextComponentId() noexcept {
    static std::size_t counter = 0;
    return counter++;
}

template <typename T>
[[nodiscard]] std::size_t componentId() noexcept {
    static const std::size_t id = nextComponentId();
    return id;
}

} // namespace detail

} // namespace rb
