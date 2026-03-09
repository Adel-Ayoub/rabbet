#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rb {

struct Uuid {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] static Uuid generate();
    [[nodiscard]] static Uuid fromString(std::string_view text);

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] bool valid() const noexcept { return high != 0 || low != 0; }

    [[nodiscard]] bool operator==(const Uuid&) const noexcept = default;
};

inline constexpr Uuid kNullUuid{};

} // namespace rb

template <>
struct std::hash<rb::Uuid> {
    [[nodiscard]] std::size_t operator()(const rb::Uuid& id) const noexcept {
        const std::size_t h = std::hash<std::uint64_t>{}(id.high);
        return h ^ (std::hash<std::uint64_t>{}(id.low) + 0x9e3779b97f4a7c15ULL + (h << 6) +
                    (h >> 2));
    }
};
