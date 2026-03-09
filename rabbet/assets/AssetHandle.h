#pragma once

#include <cstdint>

namespace rb {

// A generational handle into the AssetManager. T is a phantom type so a
// AssetHandle<Mesh> can never be passed where a AssetHandle<Texture> is wanted.
template <typename T>
struct AssetHandle {
    static constexpr std::uint32_t kInvalidIndex = ~std::uint32_t{0};

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }
    [[nodiscard]] constexpr bool operator==(const AssetHandle&) const noexcept = default;
};

} // namespace rb
