#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace rb {

// HDR -> display tone-mapping curves. ACES (Narkowicz fit) is the filmic default; Reinhard matches
// the renderer's old inline curve; Filmic is the Hable / Uncharted 2 curve. Each maps linear HDR to
// roughly [0,1] display-linear — gamma is applied separately, so the operators stay composable.
enum class TonemapOperator { ACES, Reinhard, Filmic };

// These mirror the GLSL in the composite shader exactly, so they can be unit-tested headlessly:
// the C++ here is the source of truth for the curve, and the shader is its GPU twin.

[[nodiscard]] inline glm::vec3 applyExposure(const glm::vec3& color, float ev) noexcept {
    return color * std::exp2(ev);
}

[[nodiscard]] inline float luminance(const glm::vec3& color) noexcept {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

[[nodiscard]] inline glm::vec3 tonemapReinhard(const glm::vec3& c) noexcept {
    return c / (c + glm::vec3(1.0f));
}

[[nodiscard]] inline glm::vec3 tonemapAces(const glm::vec3& x) noexcept {
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    const glm::vec3 num = x * (a * x + glm::vec3(b));
    const glm::vec3 den = x * (c * x + glm::vec3(d)) + glm::vec3(e);
    return glm::clamp(num / den, 0.0f, 1.0f);
}

[[nodiscard]] inline glm::vec3 hablePartial(const glm::vec3& x) noexcept {
    constexpr float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;
    return ((x * (a * x + glm::vec3(c * b)) + glm::vec3(d * e)) /
            (x * (a * x + glm::vec3(b)) + glm::vec3(d * f))) -
           glm::vec3(e / f);
}

[[nodiscard]] inline glm::vec3 tonemapFilmic(const glm::vec3& color) noexcept {
    const glm::vec3 white = hablePartial(glm::vec3(11.2f));
    return glm::clamp(hablePartial(color) / white, 0.0f, 1.0f);
}

[[nodiscard]] inline glm::vec3 tonemap(TonemapOperator op, const glm::vec3& color) noexcept {
    switch (op) {
    case TonemapOperator::Reinhard:
        return tonemapReinhard(color);
    case TonemapOperator::Filmic:
        return tonemapFilmic(color);
    case TonemapOperator::ACES:
    default:
        return tonemapAces(color);
    }
}

[[nodiscard]] inline glm::vec3 gammaCorrect(const glm::vec3& color, float gamma) noexcept {
    const float inv = gamma > 0.0f ? 1.0f / gamma : 1.0f;
    return glm::pow(glm::max(color, glm::vec3(0.0f)), glm::vec3(inv));
}

// Contrast pivots around mid-grey (0.18 linear); 1.0 is identity.
[[nodiscard]] inline glm::vec3 applyContrast(const glm::vec3& color, float contrast) noexcept {
    return (color - glm::vec3(0.18f)) * contrast + glm::vec3(0.18f);
}

// Saturation interpolates between luma (grey) and the colour; 1.0 is identity, 0.0 is greyscale.
[[nodiscard]] inline glm::vec3 applySaturation(const glm::vec3& color, float saturation) noexcept {
    return glm::mix(glm::vec3(luminance(color)), color, saturation);
}

} // namespace rb
