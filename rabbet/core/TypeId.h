#pragma once

#include <cstddef>

namespace rb::detail {

template <typename Family>
std::size_t nextTypeId() noexcept {
    static std::size_t counter = 0;
    return counter++;
}

template <typename Family, typename T>
[[nodiscard]] std::size_t typeId() noexcept {
    static const std::size_t id = nextTypeId<Family>();
    return id;
}

} // namespace rb::detail
