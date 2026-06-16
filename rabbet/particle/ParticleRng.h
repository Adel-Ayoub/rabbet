#pragma once

#include <cstdint>

namespace rb {

// A tiny deterministic PRNG (PCG-XSH-RR 32, O'Neill 2014) seeded per emitter. Deliberately not
// std::rand: same seed -> identical particle stream, every run and platform, so the simulation is
// reproducible and headless-testable.
struct ParticleRng {
    std::uint64_t state = 0;
    std::uint64_t inc = 1;

    explicit ParticleRng(std::uint64_t seed = 1u) noexcept { reseed(seed); }

    void reseed(std::uint64_t seed) noexcept {
        state = 0u;
        inc = (seed << 1u) | 1u;
        (void)nextU32();
        state += 0x853c49e6748fea9bULL + seed;
        (void)nextU32();
    }

    [[nodiscard]] std::uint32_t nextU32() noexcept {
        const std::uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    // Uniform float in [0, 1).
    [[nodiscard]] float next01() noexcept {
        return static_cast<float>(nextU32()) * (1.0f / 4294967296.0f);
    }

    // Uniform float in [min, max].
    [[nodiscard]] float range(float min, float max) noexcept {
        return min + (max - min) * next01();
    }
};

} // namespace rb
