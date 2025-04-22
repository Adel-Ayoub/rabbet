#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace rb {

class Entity {
public:
    using Index = std::uint32_t;
    using Version = std::uint32_t;

    static constexpr Index kInvalidIndex = ~Index{0};

    constexpr Entity() noexcept = default;
    constexpr Entity(Index index, Version version) noexcept : m_index(index), m_version(version) {}

    [[nodiscard]] constexpr Index index() const noexcept { return m_index; }
    [[nodiscard]] constexpr Version version() const noexcept { return m_version; }
    [[nodiscard]] constexpr bool valid() const noexcept { return m_index != kInvalidIndex; }

    [[nodiscard]] constexpr bool operator==(const Entity&) const noexcept = default;

private:
    Index m_index = kInvalidIndex;
    Version m_version = 0;
};

inline constexpr Entity kNullEntity{};

} // namespace rb

template <>
struct std::hash<rb::Entity> {
    [[nodiscard]] std::size_t operator()(rb::Entity e) const noexcept {
        std::size_t h = e.index();
        h ^= static_cast<std::size_t>(e.version()) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};
