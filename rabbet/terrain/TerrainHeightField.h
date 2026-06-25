#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rb {

struct Image;

// A resolution x resolution grid of normalized heights in [0, 1], row-major (index = z*res + x).
// The CPU heightfield the mesh generator consumes, produced from an image or seeded fBm noise.
// Pure, no GL: this and the mesh generator are the headless-testable units.
struct TerrainHeightField {
    std::vector<float> heights;
    int resolution = 0;

    // Edge-clamped sample so neighbour lookups at the border stay in-bounds.
    [[nodiscard]] float at(int x, int z) const noexcept {
        if (resolution <= 0) {
            return 0.0f;
        }
        const int cx = x < 0 ? 0 : (x >= resolution ? resolution - 1 : x);
        const int cz = z < 0 ? 0 : (z >= resolution ? resolution - 1 : z);
        return heights[static_cast<std::size_t>(cz) * static_cast<std::size_t>(resolution) +
                       static_cast<std::size_t>(cx)];
    }
};

// A flat field (all zero) at the given resolution.
[[nodiscard]] TerrainHeightField flatHeightField(int resolution);

// Seeded fBm value-noise field normalized to [0, 1]. Deterministic for a given
// (seed, resolution, octaves, frequency, lacunarity, gain) on every run and platform: the lattice
// is hashed through the engine's PCG32, not a sin-hash, so there are no directional artifacts.
[[nodiscard]] TerrainHeightField noiseHeightField(std::uint32_t seed, int resolution, int octaves,
                                                  float frequency, float lacunarity, float gain);

// Resamples an image's luminance into a normalized field at `resolution` (bilinear). An empty image
// yields a flat field.
[[nodiscard]] TerrainHeightField imageHeightField(const Image& image, int resolution);

} // namespace rb
