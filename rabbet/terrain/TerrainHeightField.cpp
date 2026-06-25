#include "rabbet/terrain/TerrainHeightField.h"

#include <algorithm>
#include <cmath>

#include "rabbet/particle/ParticleRng.h"
#include "rabbet/render/Image.h"

namespace rb {
namespace {

// One lattice value in [0, 1): fold the cell coords into the seed and draw a single number from the
// engine's PCG32. Deterministic and free of the directional artifacts a sin-based hash produces.
float latticeValue(int ix, int iz, std::uint32_t seed) {
    const std::uint64_t folded = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ix)) << 32) ^
                                 (static_cast<std::uint32_t>(iz) * 0x9E3779B9u) ^
                                 (static_cast<std::uint64_t>(seed) << 1);
    ParticleRng rng(folded);
    return rng.next01();
}

// Smooth value noise at a lattice-space point: bilinear blend of the four cell corners with a
// Hermite (smoothstep) fade, so the field is C1-continuous.
float valueNoise(float px, float pz, std::uint32_t seed) {
    const float fx = std::floor(px);
    const float fz = std::floor(pz);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = px - fx;
    const float tz = pz - fz;
    const float u = tx * tx * (3.0f - 2.0f * tx);
    const float w = tz * tz * (3.0f - 2.0f * tz);

    const float v00 = latticeValue(ix, iz, seed);
    const float v10 = latticeValue(ix + 1, iz, seed);
    const float v01 = latticeValue(ix, iz + 1, seed);
    const float v11 = latticeValue(ix + 1, iz + 1, seed);

    const float a = v00 + (v10 - v00) * u;
    const float b = v01 + (v11 - v01) * u;
    return a + (b - a) * w;
}

float luminanceAt(const Image& image, int x, int y) {
    const int cx = std::clamp(x, 0, image.width - 1);
    const int cy = std::clamp(y, 0, image.height - 1);
    const std::size_t base = (static_cast<std::size_t>(cy) * static_cast<std::size_t>(image.width) +
                              static_cast<std::size_t>(cx)) *
                             static_cast<std::size_t>(image.channels);
    const auto channel = [&](std::size_t c) {
        return static_cast<float>(std::to_integer<unsigned int>(image.pixels[base + c]));
    };
    if (image.channels >= 3) {
        return (0.299f * channel(0) + 0.587f * channel(1) + 0.114f * channel(2)) / 255.0f;
    }
    return channel(0) / 255.0f;
}

} // namespace

TerrainHeightField flatHeightField(int resolution) {
    TerrainHeightField field;
    field.resolution = std::max(resolution, 0);
    field.heights.assign(static_cast<std::size_t>(field.resolution) *
                             static_cast<std::size_t>(field.resolution),
                         0.0f);
    return field;
}

TerrainHeightField noiseHeightField(std::uint32_t seed, int resolution, int octaves, float frequency,
                                    float lacunarity, float gain) {
    TerrainHeightField field;
    field.resolution = std::max(resolution, 0);
    if (field.resolution <= 0) {
        return field;
    }
    field.heights.resize(static_cast<std::size_t>(field.resolution) *
                         static_cast<std::size_t>(field.resolution));

    const int octaveCount = std::max(octaves, 1);
    const float baseFrequency = std::max(frequency, 0.0001f);
    const float lac = std::max(lacunarity, 1.0f);
    const float g = std::clamp(gain, 0.0f, 1.0f);
    const float span = static_cast<float>(field.resolution - 1 <= 0 ? 1 : field.resolution - 1);

    float minHeight = 1e30f;
    float maxHeight = -1e30f;
    for (int z = 0; z < field.resolution; ++z) {
        for (int x = 0; x < field.resolution; ++x) {
            const float nx = static_cast<float>(x) / span;
            const float nz = static_cast<float>(z) / span;
            float amplitude = 1.0f;
            float octaveFrequency = baseFrequency;
            float sum = 0.0f;
            float totalAmplitude = 0.0f;
            for (int octave = 0; octave < octaveCount; ++octave) {
                // Offset each octave's lattice so the octaves do not align into visible grids.
                const std::uint32_t octaveSeed = seed + static_cast<std::uint32_t>(octave) * 1013u;
                sum += valueNoise(nx * octaveFrequency, nz * octaveFrequency, octaveSeed) * amplitude;
                totalAmplitude += amplitude;
                amplitude *= g;
                octaveFrequency *= lac;
            }
            const float height = totalAmplitude > 0.0f ? sum / totalAmplitude : 0.0f;
            field.heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(field.resolution) +
                          static_cast<std::size_t>(x)] = height;
            minHeight = std::min(minHeight, height);
            maxHeight = std::max(maxHeight, height);
        }
    }

    // Normalize to the full [0, 1] range so heightScale maps predictably regardless of the params.
    const float range = maxHeight - minHeight;
    if (range > 1e-6f) {
        for (float& h : field.heights) {
            h = (h - minHeight) / range;
        }
    } else {
        std::fill(field.heights.begin(), field.heights.end(), 0.0f);
    }
    return field;
}

TerrainHeightField imageHeightField(const Image& image, int resolution) {
    const int res = std::max(resolution, 0);
    if (res <= 0 || image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return flatHeightField(res);
    }
    TerrainHeightField field;
    field.resolution = res;
    field.heights.resize(static_cast<std::size_t>(res) * static_cast<std::size_t>(res));

    const float span = static_cast<float>(res - 1 <= 0 ? 1 : res - 1);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float u = static_cast<float>(x) / span * static_cast<float>(image.width - 1);
            const float v = static_cast<float>(z) / span * static_cast<float>(image.height - 1);
            const int x0 = static_cast<int>(std::floor(u));
            const int y0 = static_cast<int>(std::floor(v));
            const float tx = u - static_cast<float>(x0);
            const float ty = v - static_cast<float>(y0);
            const float h00 = luminanceAt(image, x0, y0);
            const float h10 = luminanceAt(image, x0 + 1, y0);
            const float h01 = luminanceAt(image, x0, y0 + 1);
            const float h11 = luminanceAt(image, x0 + 1, y0 + 1);
            const float a = h00 + (h10 - h00) * tx;
            const float b = h01 + (h11 - h01) * tx;
            field.heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(res) +
                          static_cast<std::size_t>(x)] = a + (b - a) * ty;
        }
    }
    return field;
}

} // namespace rb
