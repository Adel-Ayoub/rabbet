#include "examples/vulkan_triangle/MaterialPass.h"

#include "examples/vulkan_triangle/ObjectsPass.h"
#include "examples/vulkan_triangle/PassCommands.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/TextureConfig.h"
#include "rabbet/render/Vertex.h"
#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Barriers.h"
#include "rabbet/render/vulkan/Buffer.h"
#include "rabbet/render/vulkan/Descriptors.h"
#include "rabbet/render/vulkan/Image.h"
#include "rabbet/render/vulkan/OffscreenTarget.h"
#include "rabbet/render/vulkan/Pipeline.h"
#include "rabbet/render/vulkan/Readback.h"
#include "rabbet/render/vulkan/RetireQueue.h"
#include "rabbet/render/vulkan/Sampler.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t passWidth = 800;
constexpr std::uint32_t passHeight = 600;
constexpr std::uint32_t textureWidth = 4;
constexpr std::uint32_t textureHeight = 4;
constexpr std::size_t maximumPpmTokenLength = 10;
constexpr std::array<float, 4> clearColor{0.30F, 0.37F, 0.47F, 1.0F};
constexpr std::uint32_t pbrIndex = 0;
constexpr std::uint32_t phongIndex = 1;
constexpr std::uint32_t hdrWidth = 320;
constexpr std::uint32_t hdrHeight = 240;
constexpr std::uint32_t shadowSize = 2048;
constexpr std::uint32_t skySize = 8;
constexpr std::uint32_t irradianceSize = 32;

struct FrameBlock {
    std::array<float, 16> viewProjection{};
};
static_assert(sizeof(FrameBlock) == 64);

struct alignas(16) Vec4 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};
};
static_assert(sizeof(Vec4) == 16);

struct alignas(16) AmbientHeader {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    std::int32_t directionalCount{0};
};
static_assert(offsetof(AmbientHeader, directionalCount) == 12);
static_assert(sizeof(AmbientHeader) == 16);

struct alignas(16) CountHeader {
    std::int32_t count{0};
    std::array<std::byte, 12> padding{};
};
static_assert(sizeof(CountHeader) == 16);

struct alignas(16) EnvironmentHeader {
    std::int32_t enabled{0};
    float intensity{1.0F};
    std::array<std::byte, 8> padding{};
};
static_assert(offsetof(EnvironmentHeader, intensity) == 4);
static_assert(sizeof(EnvironmentHeader) == 16);

struct alignas(16) OutputHeader {
    std::int32_t shadowMap{0};
    std::int32_t hdr{0};
    std::array<std::byte, 8> padding{};
};
static_assert(offsetof(OutputHeader, hdr) == 4);
static_assert(sizeof(OutputHeader) == 16);

struct alignas(16) LightBlock {
    Vec4 viewPosition{};
    AmbientHeader ambient{};
    std::array<Vec4, 4> directionalDirection{};
    std::array<Vec4, 4> directionalColor{};
    CountHeader point{};
    std::array<Vec4, 8> pointPosition{};
    std::array<Vec4, 8> pointColor{};
    std::array<Vec4, 8> pointAttenuation{};
    CountHeader spot{};
    std::array<Vec4, 4> spotPosition{};
    std::array<Vec4, 4> spotDirection{};
    std::array<Vec4, 4> spotColor{};
    std::array<Vec4, 4> spotAttenuation{};
    std::array<Vec4, 4> spotCone{};
    EnvironmentHeader environment{};
    std::array<float, 16> lightSpace{};
    OutputHeader output{};
};
static_assert(offsetof(LightBlock, viewPosition) == 0);
static_assert(offsetof(LightBlock, ambient) == 16);
static_assert(offsetof(LightBlock, directionalDirection) == 32);
static_assert(offsetof(LightBlock, directionalColor) == 96);
static_assert(offsetof(LightBlock, point) == 160);
static_assert(offsetof(LightBlock, pointPosition) == 176);
static_assert(offsetof(LightBlock, pointColor) == 304);
static_assert(offsetof(LightBlock, pointAttenuation) == 432);
static_assert(offsetof(LightBlock, spot) == 560);
static_assert(offsetof(LightBlock, spotPosition) == 576);
static_assert(offsetof(LightBlock, spotDirection) == 640);
static_assert(offsetof(LightBlock, spotColor) == 704);
static_assert(offsetof(LightBlock, spotAttenuation) == 768);
static_assert(offsetof(LightBlock, spotCone) == 832);
static_assert(offsetof(LightBlock, environment) == 896);
static_assert(offsetof(LightBlock, lightSpace) == 912);
static_assert(offsetof(LightBlock, output) == 976);
static_assert(sizeof(LightBlock) == 992);

struct alignas(16) PbrBlock {
    Vec4 baseAndMetallic{};
    Vec4 emissiveAndRoughness{};
    Vec4 ao{};
};
static_assert(sizeof(PbrBlock) == 48);

struct alignas(16) PhongBlock {
    Vec4 tintAndSpecular{};
    Vec4 emissiveAndShininess{};
};
static_assert(sizeof(PhongBlock) == 32);

struct PushBlock {
    std::array<float, 16> model{};
    std::array<float, 12> normal{};
};
static_assert(sizeof(PushBlock) == 112);

struct ModelPush {
    std::array<float, 16> model{};
};
static_assert(sizeof(ModelPush) == 64);

struct ConvolvePush {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
};
static_assert(sizeof(ConvolvePush) == 128);

struct SkyboxPush {
    std::array<float, 16> viewProjection{};
    std::int32_t hdr{0};
};
static_assert(sizeof(SkyboxPush) == 68);

struct alignas(16) WaterBlock {
    float time{0.0F};
    float tileScale{0.0F};
    float strength{0.0F};
    float smoothness{0.0F};
    std::array<float, 2> extent{};
    std::int32_t hasSkybox{0};
    std::array<std::byte, 4> padding{};
    Vec4 deepColor{};
    Vec4 shallowColor{};
};
static_assert(offsetof(WaterBlock, extent) == 16);
static_assert(offsetof(WaterBlock, hasSkybox) == 24);
static_assert(offsetof(WaterBlock, deepColor) == 32);
static_assert(sizeof(WaterBlock) == 64);

struct PrefilterPush {
    float threshold{1.0F};
    float knee{0.5F};
};
static_assert(sizeof(PrefilterPush) == 8);

struct TexelPush {
    std::array<float, 2> texel{};
};
static_assert(sizeof(TexelPush) == 8);

struct UpsamplePush {
    std::array<float, 2> texel{};
    float radius{1.0F};
};
static_assert(sizeof(UpsamplePush) == 12);

struct CompositePush {
    std::int32_t bloomEnabled{1};
    float bloomIntensity{0.08F};
    float exposure{0.25F};
    std::int32_t tonemap{0};
    float gamma{2.2F};
    float contrast{1.08F};
    float saturation{0.95F};
    float vignette{0.12F};
};
static_assert(sizeof(CompositePush) == 32);

struct MeshRange {
    std::uint32_t firstIndex{0};
    std::uint32_t indexCount{0};
    std::int32_t vertexOffset{0};
};

struct HdrChecks {
    bool direct{false};
    bool hdr{false};
    bool lights{false};
    bool emissive{false};
    bool shadows{false};
    bool skybox{false};
    bool irradiance{false};
    bool environment{false};
    bool fallback{false};
    bool post{false};
    bool bloom{false};
    bool toneMapping{false};
    bool colorControls{false};
    bool gamma{false};
    bool fxaa{false};
    bool smallTargets{false};
    bool water{false};
};

struct LdrSceneChecks {
    bool materials{false};
    bool emissive{false};
};

struct LightSelection {
    bool directional{true};
    bool point{true};
    bool spot{true};
};

struct HdrShaders {
    std::vector<std::uint32_t> litVertex;
    std::vector<std::uint32_t> pbrFragment;
    std::vector<std::uint32_t> phongFragment;
    std::vector<std::uint32_t> depthVertex;
    std::vector<std::uint32_t> depthFragment;
    std::vector<std::uint32_t> skyboxVertex;
    std::vector<std::uint32_t> skyboxFragment;
    std::vector<std::uint32_t> convolveVertex;
    std::vector<std::uint32_t> convolveFragment;
    std::vector<std::uint32_t> fullscreenVertex;
    std::vector<std::uint32_t> prefilterFragment;
    std::vector<std::uint32_t> downsampleFragment;
    std::vector<std::uint32_t> upsampleFragment;
    std::vector<std::uint32_t> compositeFragment;
    std::vector<std::uint32_t> fxaaFragment;
    std::vector<std::uint32_t> waterVertex;
    std::vector<std::uint32_t> waterFragment;

    [[nodiscard]] bool valid() const noexcept {
        return !litVertex.empty() && !pbrFragment.empty() && !phongFragment.empty() &&
               !depthVertex.empty() && !depthFragment.empty() && !skyboxVertex.empty() &&
               !skyboxFragment.empty() && !convolveVertex.empty() &&
               !convolveFragment.empty() && !fullscreenVertex.empty() &&
               !prefilterFragment.empty() && !downsampleFragment.empty() &&
               !upsampleFragment.empty() && !compositeFragment.empty() &&
               !fxaaFragment.empty() && !waterVertex.empty() && !waterFragment.empty();
    }
};

struct DrawRange {
    std::uint32_t pipeline{pbrIndex};
    std::uint32_t firstIndex{0};
    std::uint32_t indexCount{0};
    std::int32_t vertexOffset{0};
    glm::mat4 model{1.0F};
};

struct BaselineImage {
    std::vector<std::byte> pixels;
};

FrameBlock makeFrame(const glm::mat4& viewProjection) {
    FrameBlock result;
    std::memcpy(result.viewProjection.data(), &viewProjection, sizeof(viewProjection));
    return result;
}

LightBlock makeLights() {
    LightBlock result;
    result.viewPosition = Vec4{3.0F, 2.4F, 4.4F, 0.0F};
    result.ambient = AmbientHeader{0.32F, 0.34F, 0.40F, 0};
    result.point.count = 1;
    result.pointPosition[0] = Vec4{1.2F, 2.2F, 1.6F, 0.0F};
    result.pointColor[0] = Vec4{2.5F, 1.5F, 0.75F, 0.0F};
    result.pointAttenuation[0] = Vec4{1.0F, 0.09F, 0.032F, 0.0F};
    const glm::mat4 identity(1.0F);
    std::memcpy(result.lightSpace.data(), &identity, sizeof(identity));
    return result;
}

LightBlock makeHdrLights(const glm::mat4& lightSpace, bool environment, bool shadow, bool hdr,
                         const LightSelection& lights) {
    LightBlock result;
    result.viewPosition = Vec4{3.6F, 2.8F, 5.2F, 0.0F};
    result.ambient = AmbientHeader{0.10F, 0.12F, 0.16F, lights.directional ? 1 : 0};
    result.directionalDirection[0] = Vec4{-0.45F, -1.0F, -0.35F, 0.0F};
    result.directionalColor[0] = Vec4{1.7F, 1.55F, 1.35F, 0.0F};
    result.point.count = lights.point ? 1 : 0;
    result.pointPosition[0] = Vec4{1.8F, 1.5F, 1.2F, 0.0F};
    result.pointColor[0] = Vec4{2.8F, 0.9F, 0.45F, 0.0F};
    result.pointAttenuation[0] = Vec4{1.0F, 0.14F, 0.07F, 0.0F};
    result.spot.count = lights.spot ? 1 : 0;
    result.spotPosition[0] = Vec4{-2.6F, 2.8F, 2.5F, 0.0F};
    result.spotDirection[0] = Vec4{0.55F, -0.65F, -0.52F, 0.0F};
    result.spotColor[0] = Vec4{0.55F, 0.95F, 2.4F, 0.0F};
    result.spotAttenuation[0] = Vec4{1.0F, 0.09F, 0.032F, 0.0F};
    result.spotCone[0] = Vec4{std::cos(glm::radians(14.0F)),
                              std::cos(glm::radians(24.0F)), 0.0F, 0.0F};
    result.environment.enabled = environment ? 1 : 0;
    result.environment.intensity = 0.7F;
    std::memcpy(result.lightSpace.data(), &lightSpace, sizeof(lightSpace));
    result.output.shadowMap = shadow ? 1 : 0;
    result.output.hdr = hdr ? 1 : 0;
    return result;
}

PushBlock makePush(const glm::mat4& model) {
    PushBlock result;
    std::memcpy(result.model.data(), &model, sizeof(model));
    const glm::mat3 normal = glm::transpose(glm::inverse(glm::mat3(model)));
    for (int column = 0; column < 3; ++column) {
        const std::size_t base = static_cast<std::size_t>(column) * 4U;
        result.normal[base] = normal[column][0];
        result.normal[base + 1U] = normal[column][1];
        result.normal[base + 2U] = normal[column][2];
    }
    return result;
}

ModelPush makeModelPush(const glm::mat4& model) {
    ModelPush result;
    std::memcpy(result.model.data(), &model, sizeof(model));
    return result;
}

ConvolvePush makeConvolvePush(const glm::mat4& view, const glm::mat4& projection) {
    ConvolvePush result;
    std::memcpy(result.view.data(), &view, sizeof(view));
    std::memcpy(result.projection.data(), &projection, sizeof(projection));
    return result;
}

SkyboxPush makeSkyboxPush(const glm::mat4& viewProjection, bool hdr) {
    SkyboxPush result;
    std::memcpy(result.viewProjection.data(), &viewProjection, sizeof(viewProjection));
    result.hdr = hdr ? 1 : 0;
    return result;
}

float halfToFloat(std::uint16_t value) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    const std::uint32_t exponent = (value >> 10U) & 0x1fU;
    const std::uint32_t fraction = value & 0x03ffU;
    std::uint32_t bits = 0;
    if (exponent == 0U) {
        if (fraction == 0U) {
            bits = sign;
        } else {
            std::uint32_t normalized = fraction;
            std::uint32_t shift = 0;
            while ((normalized & 0x0400U) == 0U) {
                normalized <<= 1U;
                ++shift;
            }
            normalized &= 0x03ffU;
            bits = sign | ((113U - shift) << 23U) | (normalized << 13U);
        }
    } else if (exponent == 0x1fU) {
        bits = sign | 0x7f800000U | (fraction << 13U);
    } else {
        bits = sign | ((exponent + 112U) << 23U) | (fraction << 13U);
    }
    return std::bit_cast<float>(bits);
}

double byteMeanDifference(const std::vector<std::byte>& first,
                          const std::vector<std::byte>& second,
                          unsigned& maximum) {
    if (first.size() != second.size() || first.empty()) {
        maximum = 255U;
        return 255.0;
    }
    std::uint64_t total = 0;
    maximum = 0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const unsigned a = std::to_integer<unsigned>(first[index]);
        const unsigned b = std::to_integer<unsigned>(second[index]);
        const unsigned delta = a > b ? a - b : b - a;
        maximum = std::max(maximum, delta);
        total += delta;
    }
    return static_cast<double>(total) / static_cast<double>(first.size());
}

bool checkHdrPixels(const std::vector<std::byte>& pixels, const char* name) {
    if (pixels.empty() || pixels.size() % 8U != 0U) {
        return false;
    }
    float maximum = 0.0F;
    std::size_t finiteChannels = 0;
    std::size_t brightChannels = 0;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 2U) {
        std::uint16_t bits = 0;
        std::memcpy(&bits, pixels.data() + offset, sizeof(bits));
        const float value = halfToFloat(bits);
        if (std::isfinite(value)) {
            ++finiteChannels;
            maximum = std::max(maximum, value);
            if (value > 1.0F) {
                ++brightChannels;
            }
        }
    }
    const bool passed = finiteChannels == pixels.size() / 2U && brightChannels > 0U;
    std::cout << "Vulkan HDR check " << name << " max=" << maximum
              << " bright_channels=" << brightChannels
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool checkIrradianceFaces(rb::vulkan::Readback& readback,
                          const rb::vulkan::Image& irradiance) {
    std::array<double, 6> means{};
    std::array<std::array<double, 3>, 6> centers{};
    std::array<double, 6> verticalDifferences{};
    std::array<double, 6> horizontalDifferences{};
    std::vector<std::byte> invalidPixels(static_cast<std::size_t>(irradianceSize) *
                                         irradianceSize * 8U);
    const bool invalidMip = !readback.readImage(
        irradiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        invalidPixels, irradiance.mipLevels(), 0U);
    const bool invalidLayer = !readback.readImage(
        irradiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        invalidPixels, 0U, irradiance.arrayLayers());
    bool passed = invalidMip && invalidLayer;
    for (std::uint32_t face = 0; face < 6U; ++face) {
        std::vector<std::byte> pixels(static_cast<std::size_t>(irradianceSize) *
                                     irradianceSize * 8U);
        if (!readback.readImage(irradiance,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, pixels, 0, face)) {
            return false;
        }
        double total = 0.0;
        for (std::size_t texel = 0; texel < pixels.size() / 8U; ++texel) {
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                std::uint16_t bits = 0;
                const std::size_t offset = texel * 8U + channel * 2U;
                std::memcpy(&bits, pixels.data() + offset, sizeof(bits));
                const float value = halfToFloat(bits);
                passed = passed && std::isfinite(value);
                if (channel < 3U) {
                    total += static_cast<double>(value);
                } else {
                    passed = passed && std::abs(value - 1.0F) <= 0.002F;
                }
            }
        }
        means[face] = total / static_cast<double>(pixels.size() / 8U * 3U);
        double top = 0.0;
        double bottom = 0.0;
        double left = 0.0;
        double right = 0.0;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const std::size_t centerOffset =
                (static_cast<std::size_t>(irradianceSize / 2U) * irradianceSize +
                 irradianceSize / 2U) *
                    8U +
                channel * 2U;
            std::uint16_t centerBits = 0;
            std::memcpy(&centerBits, pixels.data() + centerOffset, sizeof(centerBits));
            centers[face][channel] = static_cast<double>(halfToFloat(centerBits));
            for (std::uint32_t x = 0; x < irradianceSize; ++x) {
                const std::size_t topOffset =
                    (static_cast<std::size_t>(2U) * irradianceSize + x) * 8U + channel * 2U;
                const std::size_t bottomOffset =
                    (static_cast<std::size_t>(irradianceSize - 3U) * irradianceSize + x) *
                        8U +
                    channel * 2U;
                std::uint16_t topBits = 0;
                std::uint16_t bottomBits = 0;
                std::memcpy(&topBits, pixels.data() + topOffset, sizeof(topBits));
                std::memcpy(&bottomBits, pixels.data() + bottomOffset,
                            sizeof(bottomBits));
                top += static_cast<double>(halfToFloat(topBits));
                bottom += static_cast<double>(halfToFloat(bottomBits));
            }
            for (std::uint32_t y = 0; y < irradianceSize; ++y) {
                const std::size_t leftOffset =
                    (static_cast<std::size_t>(y) * irradianceSize + 2U) * 8U +
                    channel * 2U;
                const std::size_t rightOffset =
                    (static_cast<std::size_t>(y) * irradianceSize +
                     irradianceSize - 3U) *
                        8U +
                    channel * 2U;
                std::uint16_t leftBits = 0;
                std::uint16_t rightBits = 0;
                std::memcpy(&leftBits, pixels.data() + leftOffset, sizeof(leftBits));
                std::memcpy(&rightBits, pixels.data() + rightOffset, sizeof(rightBits));
                left += static_cast<double>(halfToFloat(leftBits));
                right += static_cast<double>(halfToFloat(rightBits));
            }
        }
        verticalDifferences[face] =
            (bottom - top) / static_cast<double>(irradianceSize * 3U);
        horizontalDifferences[face] =
            (right - left) / static_cast<double>(irradianceSize * 3U);
    }
    const auto [minimum, maximum] = std::minmax_element(means.begin(), means.end());
    passed = passed && *minimum > 0.01 && *maximum - *minimum > 0.01;
    constexpr std::array<std::array<double, 3>, 6> expectedCenters{{
        {0.560547, 0.213135, 0.157349}, {0.142090, 0.255127, 0.556152},
        {0.281006, 0.441406, 0.612305}, {0.215942, 0.135864, 0.143677},
        {0.617676, 0.364990, 0.238525}, {0.133301, 0.275635, 0.261475}}};
    constexpr std::array<double, 6> verticalSigns{-1.0, -1.0, 1.0, -1.0, -1.0, -1.0};
    constexpr double centerTolerance = 0.03;
    for (std::size_t face = 0; face < centers.size(); ++face) {
        for (std::size_t channel = 0; channel < centers[face].size(); ++channel) {
            passed = passed &&
                     std::abs(centers[face][channel] - expectedCenters[face][channel]) <=
                         centerTolerance;
        }
        passed = passed && verticalDifferences[face] * verticalSigns[face] > 0.05;
    }
    passed = passed && horizontalDifferences[0] < -0.05 &&
             horizontalDifferences[1] > 0.05;
    std::cout << "Vulkan irradiance means=";
    for (std::size_t face = 0; face < means.size(); ++face) {
        std::cout << (face == 0U ? "" : ",") << means[face];
    }
    std::cout << " centers=";
    for (std::size_t face = 0; face < centers.size(); ++face) {
        std::cout << (face == 0U ? "" : ";") << centers[face][0] << ','
                  << centers[face][1] << ',' << centers[face][2];
    }
    std::cout << " vertical=";
    for (std::size_t face = 0; face < verticalDifferences.size(); ++face) {
        std::cout << (face == 0U ? "" : ",") << verticalDifferences[face];
    }
    std::cout << " horizontal=";
    for (std::size_t face = 0; face < horizontalDifferences.size(); ++face) {
        std::cout << (face == 0U ? "" : ",") << horizontalDifferences[face];
    }
    std::cout << " bounds=" << (invalidMip && invalidLayer ? "pass" : "fail")
              << " center_tolerance=" << centerTolerance
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool checkDepthPixels(const std::vector<std::byte>& pixels) {
    if (pixels.empty() || pixels.size() % sizeof(float) != 0U) {
        return false;
    }
    float minimum = 1.0F;
    float maximum = 0.0F;
    bool passed = true;
    for (std::size_t offset = 0; offset < pixels.size(); offset += sizeof(float)) {
        float value = 0.0F;
        std::memcpy(&value, pixels.data() + offset, sizeof(value));
        passed = passed && std::isfinite(value) && value >= 0.0F && value <= 1.0F;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    passed = passed && minimum < 0.99F && maximum > minimum + 0.01F;
    std::cout << "Vulkan HDR shadow depth min=" << minimum << " max=" << maximum
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

std::array<unsigned, 4> pixelAtExtent(const std::vector<std::byte>& pixels, VkExtent2D extent,
                                      std::uint32_t x, std::uint32_t y) {
    const std::size_t base = (static_cast<std::size_t>(y) * extent.width + x) * 4U;
    return {std::to_integer<unsigned>(pixels[base]),
            std::to_integer<unsigned>(pixels[base + 1U]),
            std::to_integer<unsigned>(pixels[base + 2U]),
            std::to_integer<unsigned>(pixels[base + 3U])};
}

std::array<unsigned, 4> sampleWorld(const std::vector<std::byte>& pixels, VkExtent2D extent,
                                    const glm::mat4& viewProjection,
                                    const glm::vec3& world) {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const auto x = static_cast<std::uint32_t>(std::clamp(
        std::lround((ndc.x * 0.5F + 0.5F) * static_cast<float>(extent.width)), 0L,
        static_cast<long>(extent.width) - 1L));
    const auto y = static_cast<std::uint32_t>(std::clamp(
        std::lround((1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(extent.height)), 0L,
        static_cast<long>(extent.height) - 1L));
    return pixelAtExtent(pixels, extent, x, y);
}

LdrSceneChecks checkLdrScene(const std::vector<std::byte>& pixels,
                             const glm::mat4& viewProjection) {
    const auto pbr = sampleWorld(pixels, {hdrWidth, hdrHeight}, viewProjection,
                                 glm::vec3(-1.1F, 0.65F, 0.0F));
    const auto phong = sampleWorld(pixels, {hdrWidth, hdrHeight}, viewProjection,
                                   glm::vec3(0.85F, 0.55F, -0.45F));
    const auto emissive = sampleWorld(pixels, {hdrWidth, hdrHeight}, viewProjection,
                                      glm::vec3(0.25F, 1.25F, 0.9F));
    const auto brightness = [](const std::array<unsigned, 4>& pixel) {
        return pixel[0] + pixel[1] + pixel[2];
    };
    LdrSceneChecks checks;
    checks.materials = pbr[3] == 255U && phong[3] == 255U &&
                       brightness(pbr) > 50U && brightness(phong) > 50U && pbr != phong;
    checks.emissive = emissive[3] == 255U && brightness(emissive) > 500U;
    std::cout << "Vulkan HDR scene pbr=" << pbr[0] << ',' << pbr[1] << ',' << pbr[2]
              << " phong=" << phong[0] << ',' << phong[1] << ',' << phong[2]
              << " emissive=" << emissive[0] << ',' << emissive[1] << ',' << emissive[2]
              << " materials=" << (checks.materials ? "pass" : "fail")
              << " emissive_status=" << (checks.emissive ? "pass" : "fail") << '\n';
    return checks;
}

MeshRange appendMesh(const rb::MeshData& mesh, std::vector<rb::Vertex>& vertices,
                     std::vector<std::uint32_t>& indices) {
    MeshRange result;
    result.firstIndex = static_cast<std::uint32_t>(indices.size());
    result.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    result.vertexOffset = static_cast<std::int32_t>(vertices.size());
    vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    return result;
}

template <typename T>
bool updateBuffer(rb::vulkan::Buffer& buffer, const T& value) {
    std::memcpy(buffer.mapped(), &value, sizeof(value));
    return buffer.flush(0, sizeof(value));
}

void writeBufferDescriptor(VkDescriptorSet set, std::uint32_t binding,
                           const VkDescriptorBufferInfo& info, VkWriteDescriptorSet& write) {
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
}

void writeImageDescriptor(VkDescriptorSet set, std::uint32_t binding,
                          const VkDescriptorImageInfo& info, VkWriteDescriptorSet& write) {
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
}

void setViewport(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    const VkViewport viewport{0.0F, static_cast<float>(extent.height),
                              static_cast<float>(extent.width),
                              -static_cast<float>(extent.height), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void transitionColorToAttachment(VkCommandBuffer commandBuffer, const rb::vulkan::Image& image,
                                 VkImageLayout oldLayout,
                                 VkPipelineStageFlags2 sourceStage =
                                     VK_PIPELINE_STAGE_2_NONE,
                                 VkAccessFlags2 sourceAccess = VK_ACCESS_2_NONE) {
    rb::vulkan::ImageBarrier barrier{};
    barrier.image = image.handle();
    barrier.srcStage = sourceStage;
    barrier.srcAccess = sourceAccess;
    barrier.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rb::vulkan::cmdImageBarrier(commandBuffer, barrier);
}

void transitionColorToSample(VkCommandBuffer commandBuffer, const rb::vulkan::Image& image) {
    rb::vulkan::ImageBarrier barrier{};
    barrier.image = image.handle();
    barrier.srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rb::vulkan::cmdImageBarrier(commandBuffer, barrier);
}

void transitionDepthToAttachment(VkCommandBuffer commandBuffer, const rb::vulkan::Image& image,
                                 VkImageLayout oldLayout,
                                 VkPipelineStageFlags2 sourceStage =
                                     VK_PIPELINE_STAGE_2_NONE,
                                 VkAccessFlags2 sourceAccess = VK_ACCESS_2_NONE) {
    rb::vulkan::ImageBarrier barrier{};
    barrier.image = image.handle();
    barrier.srcStage = sourceStage;
    barrier.srcAccess = sourceAccess;
    barrier.dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    rb::vulkan::cmdImageBarrier(commandBuffer, barrier);
}

void beginRendering(VkCommandBuffer commandBuffer, VkExtent2D extent, VkImageView colorView,
                    VkImageView depthView, const std::array<float, 4>& color,
                    VkAttachmentLoadOp colorLoad = VK_ATTACHMENT_LOAD_OP_CLEAR) {
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = colorLoad;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{color[0], color[1], color[2], color[3]}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    if (colorView != VK_NULL_HANDLE) {
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
    }
    if (depthView != VK_NULL_HANDLE) {
        renderingInfo.pDepthAttachment = &depthAttachment;
    }
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    setViewport(commandBuffer, extent);
}

bool isPpmWhitespace(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

void skipPpmComment(std::istream& file) {
    char character = 0;
    while (file.get(character)) {
        if (character == '\r' || character == '\n') {
            return;
        }
    }
}

bool readPpmToken(std::istream& file, std::string& token, bool allowTerminatingComment = true) {
    token.clear();
    char character = 0;
    while (file.get(character)) {
        if (character == '#') {
            skipPpmComment(file);
            continue;
        }
        if (!isPpmWhitespace(character)) {
            token.push_back(character);
            if (token.size() > maximumPpmTokenLength) {
                return false;
            }
            break;
        }
    }
    if (token.empty()) {
        return false;
    }
    while (file.get(character)) {
        if (character == '#') {
            if (!allowTerminatingComment) {
                return false;
            }
            skipPpmComment(file);
            return true;
        }
        if (isPpmWhitespace(character)) {
            return true;
        }
        token.push_back(character);
        if (token.size() > maximumPpmTokenLength) {
            return false;
        }
    }
    return false;
}

bool parsePpmNumber(std::string_view token, std::uint32_t& value) {
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool parsePpm(const std::string& path, BaselineImage& image) {
    std::ifstream file(path, std::ios::binary);
    std::array<char, 2> magic{};
    char magicSeparator = 0;
    std::string widthToken;
    std::string heightToken;
    std::string maximumToken;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t maximum = 0;
    constexpr std::array<char, 2> expectedMagic{'P', '6'};
    if (!file.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
        magic != expectedMagic || !file.get(magicSeparator) || !isPpmWhitespace(magicSeparator) ||
        !readPpmToken(file, widthToken) || !readPpmToken(file, heightToken) ||
        !readPpmToken(file, maximumToken, false) || !parsePpmNumber(widthToken, width) ||
        !parsePpmNumber(heightToken, height) || !parsePpmNumber(maximumToken, maximum) ||
        maximum != 255U || width != passWidth || height != passHeight) {
        std::cerr << "Vulkan materials baseline header is invalid " << path << '\n';
        return false;
    }
    constexpr std::size_t size = static_cast<std::size_t>(passWidth) * passHeight * 3U;
    image.pixels.resize(size);
    if (!file.read(reinterpret_cast<char*>(image.pixels.data()),
                   static_cast<std::streamsize>(size))) {
        std::cerr << "Vulkan materials baseline read failed " << path << '\n';
        return false;
    }
    const int trailing = file.peek();
    if (file.bad()) {
        std::cerr << "Vulkan materials baseline read failed " << path << '\n';
        return false;
    }
    if (trailing != std::char_traits<char>::eof()) {
        std::cerr << "Vulkan materials baseline has trailing data " << path << '\n';
        return false;
    }
    return true;
}

bool writePpm(const std::string& directory, const std::vector<std::byte>& pixels) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        std::cerr << "Vulkan materials output directory failed " << directory << '\n';
        return false;
    }
    std::ofstream out(std::filesystem::path(directory) / "vk_materials.ppm", std::ios::binary);
    out << "P6\n" << passWidth << ' ' << passHeight << "\n255\n";
    for (std::size_t i = 0; i < pixels.size(); i += 4U) {
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[i])));
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[i + 1U])));
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[i + 2U])));
    }
    return static_cast<bool>(out);
}

bool writeSizedPpm(const std::filesystem::path& path, VkExtent2D extent,
                   const std::vector<std::byte>& pixels) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec || pixels.size() != static_cast<std::size_t>(extent.width) * extent.height * 4U) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    for (std::size_t index = 0; index < pixels.size(); index += 4U) {
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[index])));
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[index + 1U])));
        out.put(static_cast<char>(std::to_integer<unsigned>(pixels[index + 2U])));
    }
    return static_cast<bool>(out);
}

bool compareBaseline(const std::string& path, const std::vector<std::byte>& pixels) {
    BaselineImage baseline;
    if (!parsePpm(path, baseline)) {
        return false;
    }
    std::uint64_t totalDelta = 0;
    unsigned maximumDelta = 0;
    std::size_t compared = 0;
    for (std::uint32_t y = 0; y < passHeight; ++y) {
        for (std::uint32_t x = 0; x < passWidth; ++x) {
            const std::size_t base4 = (static_cast<std::size_t>(y) * passWidth + x) * 4U;
            const std::size_t base3 = (static_cast<std::size_t>(y) * passWidth + x) * 3U;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const unsigned ours = std::to_integer<unsigned>(pixels[base4 + channel]);
                const unsigned theirs = std::to_integer<unsigned>(baseline.pixels[base3 + channel]);
                const unsigned delta = ours > theirs ? ours - theirs : theirs - ours;
                maximumDelta = std::max(maximumDelta, delta);
                totalDelta += delta;
                ++compared;
            }
        }
    }
    constexpr unsigned tolerance = 1U;
    const bool passed = maximumDelta <= tolerance;
    std::cout << "Vulkan materials baseline max_delta=" << maximumDelta
              << " mean_delta=" << static_cast<double>(totalDelta) /
                                         static_cast<double>(compared)
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

std::array<unsigned, 4> pixelAt(const std::vector<std::byte>& pixels, std::uint32_t x,
                                std::uint32_t y) {
    const std::size_t base = (static_cast<std::size_t>(y) * passWidth + x) * 4U;
    return {std::to_integer<unsigned>(pixels[base]),
            std::to_integer<unsigned>(pixels[base + 1U]),
            std::to_integer<unsigned>(pixels[base + 2U]),
            std::to_integer<unsigned>(pixels[base + 3U])};
}

bool checkColor(const std::vector<std::byte>& pixels, const glm::mat4& viewProjection,
                const glm::vec3& world, const std::array<unsigned, 3>& expected,
                const char* name) {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const auto x = static_cast<std::uint32_t>(std::clamp(
        std::lround((ndc.x * 0.5F + 0.5F) * static_cast<float>(passWidth)), 0L,
        static_cast<long>(passWidth) - 1L));
    const auto y = static_cast<std::uint32_t>(std::clamp(
        std::lround((1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(passHeight)), 0L,
        static_cast<long>(passHeight) - 1L));
    const std::array<unsigned, 4> pixel = pixelAt(pixels, x, y);
    constexpr unsigned tolerance = 8U;
    bool passed = pixel[3] == 255U;
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        const unsigned delta = pixel[channel] > expected[channel]
                                   ? pixel[channel] - expected[channel]
                                   : expected[channel] - pixel[channel];
        passed = passed && delta <= tolerance;
    }
    std::cout << "Vulkan materials check " << name << " pixel=" << x << ',' << y
              << " expected=" << expected[0] << ',' << expected[1] << ',' << expected[2]
              << " actual=" << pixel[0] << ',' << pixel[1] << ',' << pixel[2]
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool runChecks(const rb::vulkan::Device& device, const MaterialPassPaths& paths) {
    auto allocator = rb::vulkan::Allocator::create(device);
    if (allocator == nullptr) {
        return false;
    }
    const auto vertexCode = loadSpirvFile(paths.vertexSpv);
    const auto pbrCode = loadSpirvFile(paths.pbrFragmentSpv);
    const auto phongCode = loadSpirvFile(paths.phongFragmentSpv);
    if (vertexCode.empty() || pbrCode.empty() || phongCode.empty()) {
        return false;
    }

    const glm::mat4 view = glm::lookAt(glm::vec3(3.0F, 2.4F, 4.4F),
                                       glm::vec3(0.0F, 0.5F, 0.0F),
                                       glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 projection =
        glm::perspective(glm::radians(50.0F),
                         static_cast<float>(passWidth) / static_cast<float>(passHeight), 0.1F,
                         200.0F);
    glm::mat4 clipFix(1.0F);
    clipFix[2][2] = 0.5F;
    clipFix[3][2] = 0.5F;
    const glm::mat4 viewProjection = clipFix * projection * view;

    const rb::MeshData plane = rb::geometry::quad();
    const rb::MeshData sphere = rb::geometry::sphere();
    const rb::MeshData cube = rb::geometry::cube();
    std::vector<rb::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<DrawRange, 3> draws{};
    const std::array<const rb::MeshData*, 3> meshes{&plane, &sphere, &cube};
    const std::array<glm::mat4, 3> models{
        glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, -0.5F, 0.0F)) *
            glm::rotate(glm::mat4(1.0F), glm::radians(-90.0F), glm::vec3(1.0F, 0.0F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(10.0F, 10.0F, 1.0F)),
        glm::translate(glm::mat4(1.0F), glm::vec3(-1.25F, 0.65F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(1.3F)),
        glm::translate(glm::mat4(1.0F), glm::vec3(0.55F, 0.5F, -0.6F))};
    const std::array<std::uint32_t, 3> drawPipelines{pbrIndex, pbrIndex, phongIndex};
    for (std::size_t i = 0; i < draws.size(); ++i) {
        DrawRange& draw = draws[i];
        draw.pipeline = drawPipelines[i];
        draw.firstIndex = static_cast<std::uint32_t>(indices.size());
        draw.indexCount = static_cast<std::uint32_t>(meshes[i]->indices.size());
        draw.vertexOffset = static_cast<std::int32_t>(vertices.size());
        draw.model = models[i];
        vertices.insert(vertices.end(), meshes[i]->vertices.begin(), meshes[i]->vertices.end());
        indices.insert(indices.end(), meshes[i]->indices.begin(), meshes[i]->indices.end());
    }

    const VkDeviceSize vertexBytes = vertices.size() * sizeof(rb::Vertex);
    const VkDeviceSize indexBytes = indices.size() * sizeof(std::uint32_t);
    auto vertexBuffer = rb::vulkan::Buffer::create(
        device, *allocator, vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        rb::vulkan::MemoryClass::deviceOnly);
    auto indexBuffer = rb::vulkan::Buffer::create(
        device, *allocator, indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        rb::vulkan::MemoryClass::deviceOnly);
    auto vertexStaging = rb::vulkan::Buffer::create(
        device, *allocator, vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        rb::vulkan::MemoryClass::hostUpload);
    auto indexStaging = rb::vulkan::Buffer::create(
        device, *allocator, indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        rb::vulkan::MemoryClass::hostUpload);
    auto texelStaging = rb::vulkan::Buffer::create(
        device, *allocator, textureWidth * textureHeight * 4U,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, rb::vulkan::MemoryClass::hostUpload);
    const FrameBlock frameBlock = makeFrame(viewProjection);
    const LightBlock lightBlock = makeLights();
    const PbrBlock pbrBlock{Vec4{0.78F, 0.38F, 0.22F, 0.15F},
                            Vec4{0.0F, 0.0F, 0.0F, 0.42F}, Vec4{1.0F, 0.0F, 0.0F, 0.0F}};
    const PhongBlock phongBlock{Vec4{1.0F, 1.0F, 1.0F, 0.65F},
                                Vec4{0.0F, 0.0F, 0.0F, 48.0F}};
    auto frameBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(frameBlock),
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   rb::vulkan::MemoryClass::hostUpload);
    auto lightBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(lightBlock),
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   rb::vulkan::MemoryClass::hostUpload);
    auto pbrBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(pbrBlock),
                                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                 rb::vulkan::MemoryClass::hostUpload);
    auto phongBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(phongBlock),
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   rb::vulkan::MemoryClass::hostUpload);
    if (vertexBuffer == nullptr || indexBuffer == nullptr || vertexStaging == nullptr ||
        indexStaging == nullptr || texelStaging == nullptr || frameBuffer == nullptr ||
        lightBuffer == nullptr || pbrBuffer == nullptr || phongBuffer == nullptr) {
        return false;
    }

    std::array<std::uint8_t, textureWidth * textureHeight * 4U> texels{};
    for (std::uint32_t y = 0; y < textureHeight; ++y) {
        for (std::uint32_t x = 0; x < textureWidth; ++x) {
            const std::size_t base = (static_cast<std::size_t>(y) * textureWidth + x) * 4U;
            const bool bright = (x + y) % 2U == 0U;
            texels[base] = bright ? std::uint8_t{205} : std::uint8_t{92};
            texels[base + 1U] = bright ? std::uint8_t{150} : std::uint8_t{62};
            texels[base + 2U] = bright ? std::uint8_t{90} : std::uint8_t{38};
            texels[base + 3U] = std::uint8_t{255};
        }
    }
    std::memcpy(vertexStaging->mapped(), vertices.data(), vertexBytes);
    std::memcpy(indexStaging->mapped(), indices.data(), indexBytes);
    std::memcpy(texelStaging->mapped(), texels.data(), sizeof(texels));
    std::memcpy(frameBuffer->mapped(), &frameBlock, sizeof(frameBlock));
    std::memcpy(lightBuffer->mapped(), &lightBlock, sizeof(lightBlock));
    std::memcpy(pbrBuffer->mapped(), &pbrBlock, sizeof(pbrBlock));
    std::memcpy(phongBuffer->mapped(), &phongBlock, sizeof(phongBlock));
    if (!vertexStaging->flush(0, vertexBytes) || !indexStaging->flush(0, indexBytes) ||
        !texelStaging->flush(0, sizeof(texels)) ||
        !frameBuffer->flush(0, sizeof(frameBlock)) ||
        !lightBuffer->flush(0, sizeof(lightBlock)) ||
        !pbrBuffer->flush(0, sizeof(pbrBlock)) ||
        !phongBuffer->flush(0, sizeof(phongBlock))) {
        return false;
    }

    rb::vulkan::ImageDescription albedoDescription{};
    albedoDescription.extent = {textureWidth, textureHeight};
    albedoDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
    albedoDescription.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto albedo = rb::vulkan::Image::create(device, *allocator, albedoDescription);
    rb::vulkan::ImageDescription fallbackDescription{};
    fallbackDescription.extent = {1U, 1U};
    fallbackDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    fallbackDescription.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    fallbackDescription.arrayLayers = 6;
    fallbackDescription.cube = true;
    auto irradiance = rb::vulkan::Image::create(device, *allocator, fallbackDescription);
    fallbackDescription.arrayLayers = 1;
    fallbackDescription.cube = false;
    auto shadow = rb::vulkan::Image::create(device, *allocator, fallbackDescription);
    rb::vulkan::ImageDescription colorDescription{};
    colorDescription.extent = {passWidth, passHeight};
    colorDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorDescription.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto colorTarget = rb::vulkan::Image::create(device, *allocator, colorDescription);
    rb::vulkan::ImageDescription depthDescription{};
    depthDescription.extent = {passWidth, passHeight};
    depthDescription.format = VK_FORMAT_D32_SFLOAT;
    depthDescription.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    auto depthTarget = rb::vulkan::Image::create(device, *allocator, depthDescription);
    if (albedo == nullptr || irradiance == nullptr || shadow == nullptr || colorTarget == nullptr ||
        depthTarget == nullptr) {
        return false;
    }

    PassCommands commands;
    if (!commands.create(device.handle(), device.graphicsQueueFamily(), "materials") ||
        !commands.begin()) {
        return false;
    }
    const VkBufferCopy vertexRegion{0, 0, vertexBytes};
    const VkBufferCopy indexRegion{0, 0, indexBytes};
    vkCmdCopyBuffer(commands.buffer, vertexStaging->handle(), vertexBuffer->handle(), 1,
                    &vertexRegion);
    vkCmdCopyBuffer(commands.buffer, indexStaging->handle(), indexBuffer->handle(), 1,
                    &indexRegion);
    std::array<rb::vulkan::ImageBarrier, 3> toTransfer{};
    toTransfer[0].image = albedo->handle();
    toTransfer[0].dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer[0].dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer[1] = toTransfer[0];
    toTransfer[1].image = irradiance->handle();
    toTransfer[1].arrayLayers = 6;
    toTransfer[2] = toTransfer[0];
    toTransfer[2].image = shadow->handle();
    rb::vulkan::cmdBarriers(commands.buffer, toTransfer, {});
    VkBufferImageCopy albedoCopy{};
    albedoCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    albedoCopy.imageSubresource.layerCount = 1;
    albedoCopy.imageExtent = {textureWidth, textureHeight, 1U};
    vkCmdCopyBufferToImage(commands.buffer, texelStaging->handle(), albedo->handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &albedoCopy);
    VkBufferImageCopy fallbackCopy{};
    fallbackCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    fallbackCopy.imageSubresource.layerCount = 6;
    fallbackCopy.imageExtent = {1U, 1U, 1U};
    vkCmdCopyBufferToImage(commands.buffer, texelStaging->handle(), irradiance->handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fallbackCopy);
    fallbackCopy.imageSubresource.layerCount = 1;
    vkCmdCopyBufferToImage(commands.buffer, texelStaging->handle(), shadow->handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fallbackCopy);
    std::array<rb::vulkan::ImageBarrier, 3> toSampled{};
    for (std::size_t i = 0; i < toSampled.size(); ++i) {
        toSampled[i].image = toTransfer[i].image;
        toSampled[i].srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        toSampled[i].srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toSampled[i].dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toSampled[i].dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toSampled[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSampled[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toSampled[i].arrayLayers = toTransfer[i].arrayLayers;
    }
    rb::vulkan::BufferBarrier vertexReady{};
    vertexReady.buffer = vertexBuffer->handle();
    vertexReady.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    vertexReady.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vertexReady.dstStage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    vertexReady.dstAccess = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    rb::vulkan::BufferBarrier indexReady = vertexReady;
    indexReady.buffer = indexBuffer->handle();
    indexReady.dstStage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    indexReady.dstAccess = VK_ACCESS_2_INDEX_READ_BIT;
    const std::array<rb::vulkan::BufferBarrier, 2> bufferReady{vertexReady, indexReady};
    rb::vulkan::cmdBarriers(commands.buffer, toSampled, bufferReady);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    rb::vulkan::RetireQueue retireQueue;
    retireQueue.beginFrame(1);
    retireQueue.retire(std::move(vertexStaging));
    retireQueue.retire(std::move(indexStaging));
    retireQueue.retire(std::move(texelStaging));
    retireQueue.collect(1);

    const std::array<VkDescriptorSetLayoutBinding, 4> frameBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    const std::array<VkDescriptorSetLayoutBinding, 2> materialBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto frameLayout = rb::vulkan::DescriptorSetLayout::create(device, frameBindings);
    auto materialLayout = rb::vulkan::DescriptorSetLayout::create(device, materialBindings);
    if (frameLayout == nullptr || materialLayout == nullptr) {
        return false;
    }
    const std::array<VkDescriptorPoolSize, 2> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}};
    auto pool = rb::vulkan::DescriptorPool::create(device, 3, poolSizes);
    if (pool == nullptr) {
        return false;
    }
    const VkDescriptorSet frameSet = pool->allocate(frameLayout->handle());
    const std::array<VkDescriptorSet, 2> materialSets{
        pool->allocate(materialLayout->handle()), pool->allocate(materialLayout->handle())};
    if (frameSet == VK_NULL_HANDLE || materialSets[0] == VK_NULL_HANDLE ||
        materialSets[1] == VK_NULL_HANDLE) {
        return false;
    }
    rb::SamplerConfig samplerConfig{};
    auto sampler = rb::vulkan::Sampler::create(device, samplerConfig);
    if (sampler == nullptr) {
        return false;
    }
    const std::array<VkDescriptorBufferInfo, 4> bufferInfos{
        VkDescriptorBufferInfo{frameBuffer->handle(), 0, sizeof(frameBlock)},
        VkDescriptorBufferInfo{lightBuffer->handle(), 0, sizeof(lightBlock)},
        VkDescriptorBufferInfo{pbrBuffer->handle(), 0, sizeof(pbrBlock)},
        VkDescriptorBufferInfo{phongBuffer->handle(), 0, sizeof(phongBlock)}};
    const std::array<VkDescriptorImageInfo, 3> imageInfos{
        VkDescriptorImageInfo{sampler->handle(), irradiance->view(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkDescriptorImageInfo{sampler->handle(), shadow->view(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkDescriptorImageInfo{sampler->handle(), albedo->view(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    std::array<VkWriteDescriptorSet, 8> writes{};
    const auto writeBuffer = [&](std::size_t index, VkDescriptorSet set, std::uint32_t binding,
                                 const VkDescriptorBufferInfo* info) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = set;
        writes[index].dstBinding = binding;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[index].pBufferInfo = info;
    };
    const auto writeImage = [&](std::size_t index, VkDescriptorSet set, std::uint32_t binding,
                                const VkDescriptorImageInfo* info) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = set;
        writes[index].dstBinding = binding;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[index].pImageInfo = info;
    };
    writeBuffer(0, frameSet, 0, &bufferInfos[0]);
    writeBuffer(1, frameSet, 1, &bufferInfos[1]);
    writeImage(2, frameSet, 2, &imageInfos[0]);
    writeImage(3, frameSet, 3, &imageInfos[1]);
    writeBuffer(4, materialSets[0], 0, &bufferInfos[2]);
    writeImage(5, materialSets[0], 1, &imageInfos[2]);
    writeBuffer(6, materialSets[1], 0, &bufferInfos[3]);
    writeImage(7, materialSets[1], 1, &imageInfos[2]);
    vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    const std::array<rb::vulkan::VertexAttribute, 3> attributes{
        rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT,
                                    static_cast<std::uint32_t>(offsetof(rb::Vertex, position))},
        rb::vulkan::VertexAttribute{1, VK_FORMAT_R32G32B32_SFLOAT,
                                    static_cast<std::uint32_t>(offsetof(rb::Vertex, normal))},
        rb::vulkan::VertexAttribute{2, VK_FORMAT_R32G32_SFLOAT,
                                    static_cast<std::uint32_t>(offsetof(rb::Vertex, uv))}};
    const std::array<VkFormat, 1> colorFormats{VK_FORMAT_R8G8B8A8_UNORM};
    const std::array<VkDescriptorSetLayout, 2> setLayouts{frameLayout->handle(),
                                                          materialLayout->handle()};
    std::array<std::unique_ptr<rb::vulkan::Pipeline>, 2> pipelineObjects;
    const std::array<std::span<const std::uint32_t>, 2> fragmentCodes{pbrCode, phongCode};
    for (std::size_t i = 0; i < pipelineObjects.size(); ++i) {
        rb::vulkan::PipelineDescription description{};
        description.vertexCode = vertexCode;
        description.fragmentCode = fragmentCodes[i];
        description.vertexStride = sizeof(rb::Vertex);
        description.vertexAttributes = attributes;
        description.depthTest = true;
        description.depthWrite = true;
        description.colorFormats = colorFormats;
        description.depthFormat = VK_FORMAT_D32_SFLOAT;
        description.setLayouts = setLayouts;
        description.pushConstantBytes = sizeof(PushBlock);
        description.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        pipelineObjects[i] = rb::vulkan::Pipeline::create(device, description, VK_NULL_HANDLE);
        if (pipelineObjects[i] == nullptr) {
            return false;
        }
    }

    if (!commands.begin()) {
        return false;
    }
    rb::vulkan::ImageBarrier colorBarrier{};
    colorBarrier.image = colorTarget->handle();
    colorBarrier.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    colorBarrier.dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rb::vulkan::ImageBarrier depthBarrier{};
    depthBarrier.image = depthTarget->handle();
    depthBarrier.dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthBarrier.dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthBarrier.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    const std::array<rb::vulkan::ImageBarrier, 2> renderBarriers{colorBarrier, depthBarrier};
    rb::vulkan::cmdBarriers(commands.buffer, renderBarriers, {});
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorTarget->view();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthTarget->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = {passWidth, passHeight};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commands.buffer, &renderingInfo);
    const VkViewport viewport{0.0F, static_cast<float>(passHeight), static_cast<float>(passWidth),
                              -static_cast<float>(passHeight), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, {passWidth, passHeight}};
    vkCmdSetViewport(commands.buffer, 0, 1, &viewport);
    vkCmdSetScissor(commands.buffer, 0, 1, &scissor);
    const VkBuffer vertexHandle = vertexBuffer->handle();
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
    vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    for (const DrawRange& draw : draws) {
        const auto pipelineIndex = static_cast<std::size_t>(draw.pipeline);
        vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineObjects[pipelineIndex]->handle());
        const std::array<VkDescriptorSet, 2> sets{frameSet, materialSets[pipelineIndex]};
        vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineObjects[pipelineIndex]->layout(), 0,
                                static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
        const PushBlock push = makePush(draw.model);
        vkCmdPushConstants(commands.buffer, pipelineObjects[pipelineIndex]->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(commands.buffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset,
                         0);
    }
    vkCmdEndRendering(commands.buffer);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    std::vector<std::byte> pixels(static_cast<std::size_t>(passWidth) * passHeight * 4U);
    auto readback = rb::vulkan::Readback::create(device, *allocator);
    if (readback == nullptr ||
        !readback->readImage(*colorTarget,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, pixels)) {
        return false;
    }
    const bool pbrPassed = checkColor(pixels, viewProjection, glm::vec3(-1.25F, 0.65F, 0.0F),
                                      {105U, 61U, 37U}, "pbr");
    const bool phongPassed = checkColor(pixels, viewProjection, glm::vec3(0.55F, 0.5F, -0.6F),
                                        {154U, 52U, 13U}, "phong");
    bool passed = pbrPassed && phongPassed;
    if (!paths.baselinePpm.empty()) {
        passed = compareBaseline(paths.baselinePpm, pixels) && passed;
    }
    if (!paths.outputDirectory.empty()) {
        passed = writePpm(paths.outputDirectory, pixels) && passed;
    }
    readback.reset();

    retireQueue.beginFrame(2);
    retireQueue.retire(std::move(vertexBuffer));
    retireQueue.retire(std::move(indexBuffer));
    retireQueue.retire(std::move(frameBuffer));
    retireQueue.retire(std::move(lightBuffer));
    retireQueue.retire(std::move(pbrBuffer));
    retireQueue.retire(std::move(phongBuffer));
    retireQueue.retire(std::move(albedo));
    retireQueue.retire(std::move(irradiance));
    retireQueue.retire(std::move(shadow));
    retireQueue.retire(std::move(colorTarget));
    retireQueue.retire(std::move(depthTarget));
    retireQueue.retire(std::move(sampler));
    retireQueue.collect(2);
    std::cout << "Vulkan materials allocator blocks=" << allocator->stats().blockCount
              << " live=" << allocator->stats().allocationCount << '\n';
    return passed && allocator->stats().allocationCount == 0U;
}

bool runHdrChecks(const rb::vulkan::Device& device, const MaterialPassPaths& paths) {
    HdrShaders shaders{loadSpirvFile(paths.vertexSpv),
                       loadSpirvFile(paths.pbrFragmentSpv),
                       loadSpirvFile(paths.phongFragmentSpv),
                       loadSpirvFile(paths.depthVertexSpv),
                       loadSpirvFile(paths.depthFragmentSpv),
                       loadSpirvFile(paths.skyboxVertexSpv),
                       loadSpirvFile(paths.skyboxFragmentSpv),
                       loadSpirvFile(paths.convolveVertexSpv),
                       loadSpirvFile(paths.convolveFragmentSpv),
                       loadSpirvFile(paths.fullscreenVertexSpv),
                       loadSpirvFile(paths.prefilterFragmentSpv),
                       loadSpirvFile(paths.downsampleFragmentSpv),
                       loadSpirvFile(paths.upsampleFragmentSpv),
                       loadSpirvFile(paths.compositeFragmentSpv),
                       loadSpirvFile(paths.fxaaFragmentSpv),
                       loadSpirvFile(paths.waterVertexSpv),
                       loadSpirvFile(paths.waterFragmentSpv)};
    if (!shaders.valid()) {
        std::cerr << "Vulkan HDR shader load failed\n";
        return false;
    }
    auto allocator = rb::vulkan::Allocator::create(device);
    if (allocator == nullptr) {
        return false;
    }

    const bool passed = [&]() -> bool {
        const rb::vulkan::AllocatorStats emptyStats = allocator->stats();
        rb::vulkan::ImageDescription invalidCube{};
        invalidCube.extent = {4U, 4U};
        invalidCube.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        invalidCube.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        invalidCube.arrayLayers = 5U;
        invalidCube.cube = true;
        if (rb::vulkan::Image::create(device, *allocator, invalidCube) != nullptr ||
            allocator->stats().allocationCount != emptyStats.allocationCount) {
            std::cerr << "Vulkan HDR invalid cubemap cleanup failed\n";
            return false;
        }
        rb::vulkan::PipelineDescription invalidPipeline{};
        if (rb::vulkan::Pipeline::create(device, invalidPipeline, VK_NULL_HANDLE) != nullptr) {
            std::cerr << "Vulkan HDR invalid pipeline check failed\n";
            return false;
        }

        std::vector<rb::Vertex> vertices;
        std::vector<std::uint32_t> indices;
        const rb::MeshData cube = rb::geometry::cube();
        const rb::MeshData plane = rb::geometry::quad();
        const rb::MeshData sphere = rb::geometry::sphere();
        rb::MeshData water;
        water.vertices = {{{-1.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
                          {{1.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
                          {{1.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
                          {{-1.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}}};
        water.indices = {0U, 1U, 2U, 0U, 2U, 3U};
        const MeshRange cubeRange = appendMesh(cube, vertices, indices);
        const MeshRange planeRange = appendMesh(plane, vertices, indices);
        const MeshRange sphereRange = appendMesh(sphere, vertices, indices);
        const MeshRange waterRange = appendMesh(water, vertices, indices);
        static_cast<void>(planeRange);
        static_cast<void>(sphereRange);
        static_cast<void>(waterRange);
        const VkDeviceSize vertexBytes = vertices.size() * sizeof(rb::Vertex);
        const VkDeviceSize indexBytes = indices.size() * sizeof(std::uint32_t);

        auto vertexBuffer = rb::vulkan::Buffer::create(
            device, *allocator, vertexBytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            rb::vulkan::MemoryClass::deviceOnly);
        auto indexBuffer = rb::vulkan::Buffer::create(
            device, *allocator, indexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            rb::vulkan::MemoryClass::deviceOnly);
        auto vertexStaging = rb::vulkan::Buffer::create(
            device, *allocator, vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        auto indexStaging = rb::vulkan::Buffer::create(
            device, *allocator, indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        constexpr VkDeviceSize skyBytes = skySize * skySize * 4U * 6U;
        constexpr VkDeviceSize albedoBytes = textureWidth * textureHeight * 4U;
        auto skyStaging = rb::vulkan::Buffer::create(
            device, *allocator, skyBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        auto albedoStaging = rb::vulkan::Buffer::create(
            device, *allocator, albedoBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        if (vertexBuffer == nullptr || indexBuffer == nullptr || vertexStaging == nullptr ||
            indexStaging == nullptr || skyStaging == nullptr || albedoStaging == nullptr) {
            return false;
        }

        std::memcpy(vertexStaging->mapped(), vertices.data(), vertexBytes);
        std::memcpy(indexStaging->mapped(), indices.data(), indexBytes);
        constexpr std::array<std::array<std::uint8_t, 3>, 6> faceColors{{
            {220U, 80U, 55U}, {35U, 110U, 220U}, {90U, 190U, 235U},
            {30U, 25U, 55U}, {235U, 175U, 70U}, {55U, 145U, 85U}}};
        auto* skyTexels = static_cast<std::uint8_t*>(skyStaging->mapped());
        for (std::size_t face = 0; face < faceColors.size(); ++face) {
            for (std::uint32_t y = 0; y < skySize; ++y) {
                for (std::uint32_t x = 0; x < skySize; ++x) {
                    const std::size_t base =
                        ((face * skySize + y) * skySize + x) * 4U;
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        const unsigned value = faceColors[face][channel] + x * 2U + y;
                        skyTexels[base + channel] =
                            static_cast<std::uint8_t>(std::min(value, 255U));
                    }
                    skyTexels[base + 3U] = 255U;
                }
            }
        }
        auto* albedoTexels = static_cast<std::uint8_t*>(albedoStaging->mapped());
        for (std::uint32_t y = 0; y < textureHeight; ++y) {
            for (std::uint32_t x = 0; x < textureWidth; ++x) {
                const std::size_t base =
                    (static_cast<std::size_t>(y) * textureWidth + x) * 4U;
                const bool bright = (x + y) % 2U == 0U;
                albedoTexels[base] = bright ? 220U : 115U;
                albedoTexels[base + 1U] = bright ? 175U : 75U;
                albedoTexels[base + 2U] = bright ? 120U : 45U;
                albedoTexels[base + 3U] = 255U;
            }
        }
        if (!vertexStaging->flush(0, vertexBytes) ||
            !indexStaging->flush(0, indexBytes) || !skyStaging->flush(0, skyBytes) ||
            !albedoStaging->flush(0, albedoBytes)) {
            return false;
        }

        rb::vulkan::ImageDescription skyDescription{};
        skyDescription.extent = {skySize, skySize};
        skyDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
        skyDescription.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        skyDescription.arrayLayers = 6U;
        skyDescription.cube = true;
        auto sky = rb::vulkan::Image::create(device, *allocator, skyDescription);
        rb::vulkan::ImageDescription irradianceDescription{};
        irradianceDescription.extent = {irradianceSize, irradianceSize};
        irradianceDescription.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        irradianceDescription.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        irradianceDescription.arrayLayers = 6U;
        irradianceDescription.cube = true;
        auto irradiance = rb::vulkan::Image::create(device, *allocator,
                                                    irradianceDescription);
        rb::vulkan::ImageDescription albedoDescription{};
        albedoDescription.extent = {textureWidth, textureHeight};
        albedoDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
        albedoDescription.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto albedo = rb::vulkan::Image::create(device, *allocator, albedoDescription);
        rb::vulkan::OffscreenTargetDescription shadowDescription{};
        shadowDescription.extent = {shadowSize, shadowSize};
        shadowDescription.depth = true;
        shadowDescription.sampledDepth = true;
        auto shadow = rb::vulkan::OffscreenTarget::create(device, *allocator,
                                                          shadowDescription);
        rb::vulkan::OffscreenTargetDescription hdrDescription{};
        hdrDescription.extent = {hdrWidth, hdrHeight};
        hdrDescription.colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        hdrDescription.depth = true;
        hdrDescription.sampledColor = true;
        auto hdrTarget = rb::vulkan::OffscreenTarget::create(device, *allocator,
                                                             hdrDescription);
        rb::vulkan::OffscreenTargetDescription directDescription = hdrDescription;
        directDescription.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        auto directTarget = rb::vulkan::OffscreenTarget::create(device, *allocator,
                                                                directDescription);
        if (sky == nullptr || irradiance == nullptr || albedo == nullptr || shadow == nullptr ||
            shadow->depth() == nullptr || hdrTarget == nullptr || hdrTarget->color() == nullptr ||
            hdrTarget->depth() == nullptr || directTarget == nullptr ||
            directTarget->color() == nullptr || directTarget->depth() == nullptr) {
            return false;
        }
        for (std::uint32_t face = 0; face < 6U; ++face) {
            if (sky->layerView(face) == VK_NULL_HANDLE ||
                irradiance->layerView(face) == VK_NULL_HANDLE) {
                std::cerr << "Vulkan HDR cubemap face view is missing\n";
                return false;
            }
        }

        PassCommands commands;
        if (!commands.create(device.handle(), device.graphicsQueueFamily(), "HDR") ||
            !commands.begin()) {
            return false;
        }
        const VkBufferCopy vertexCopy{0, 0, vertexBytes};
        const VkBufferCopy indexCopy{0, 0, indexBytes};
        vkCmdCopyBuffer(commands.buffer, vertexStaging->handle(), vertexBuffer->handle(), 1,
                        &vertexCopy);
        vkCmdCopyBuffer(commands.buffer, indexStaging->handle(), indexBuffer->handle(), 1,
                        &indexCopy);
        std::array<rb::vulkan::ImageBarrier, 2> uploadBarriers{};
        uploadBarriers[0].image = sky->handle();
        uploadBarriers[0].dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        uploadBarriers[0].dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        uploadBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        uploadBarriers[0].arrayLayers = 6U;
        uploadBarriers[1] = uploadBarriers[0];
        uploadBarriers[1].image = albedo->handle();
        uploadBarriers[1].arrayLayers = 1U;
        rb::vulkan::cmdBarriers(commands.buffer, uploadBarriers, {});
        VkBufferImageCopy skyCopy{};
        skyCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        skyCopy.imageSubresource.layerCount = 6U;
        skyCopy.imageExtent = {skySize, skySize, 1U};
        vkCmdCopyBufferToImage(commands.buffer, skyStaging->handle(), sky->handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &skyCopy);
        VkBufferImageCopy albedoCopy{};
        albedoCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        albedoCopy.imageSubresource.layerCount = 1U;
        albedoCopy.imageExtent = {textureWidth, textureHeight, 1U};
        vkCmdCopyBufferToImage(commands.buffer, albedoStaging->handle(), albedo->handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &albedoCopy);
        std::array<rb::vulkan::ImageBarrier, 2> sampledBarriers{};
        for (std::size_t index = 0; index < sampledBarriers.size(); ++index) {
            sampledBarriers[index].image = uploadBarriers[index].image;
            sampledBarriers[index].srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
            sampledBarriers[index].srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            sampledBarriers[index].dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            sampledBarriers[index].dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            sampledBarriers[index].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            sampledBarriers[index].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sampledBarriers[index].arrayLayers = uploadBarriers[index].arrayLayers;
        }
        rb::vulkan::BufferBarrier vertexReady{};
        vertexReady.buffer = vertexBuffer->handle();
        vertexReady.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        vertexReady.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vertexReady.dstStage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        vertexReady.dstAccess = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        rb::vulkan::BufferBarrier indexReady = vertexReady;
        indexReady.buffer = indexBuffer->handle();
        indexReady.dstStage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        indexReady.dstAccess = VK_ACCESS_2_INDEX_READ_BIT;
        const std::array<rb::vulkan::BufferBarrier, 2> geometryReady{vertexReady, indexReady};
        rb::vulkan::cmdBarriers(commands.buffer, sampledBarriers, geometryReady);
        if (!commands.submitAndWait(device.graphicsQueue())) {
            return false;
        }
        vertexStaging.reset();
        indexStaging.reset();
        skyStaging.reset();
        albedoStaging.reset();

        const std::array<VkDescriptorSetLayoutBinding, 4> frameBindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                         VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        const std::array<VkDescriptorSetLayoutBinding, 2> materialBindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        const std::array<VkDescriptorSetLayoutBinding, 1> sampleBindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        const std::array<VkDescriptorSetLayoutBinding, 2> compositeBindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto frameLayout = rb::vulkan::DescriptorSetLayout::create(device, frameBindings);
        auto materialLayout = rb::vulkan::DescriptorSetLayout::create(device, materialBindings);
        auto sampleLayout = rb::vulkan::DescriptorSetLayout::create(device, sampleBindings);
        auto compositeLayout = rb::vulkan::DescriptorSetLayout::create(device,
                                                                       compositeBindings);
        const std::array<VkDescriptorPoolSize, 2> poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16U},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 96U}};
        auto pool = rb::vulkan::DescriptorPool::create(device, 64U, poolSizes);
        rb::SamplerConfig samplerConfig{};
        samplerConfig.address = rb::TextureAddress::ClampToEdge;
        auto sampler = rb::vulkan::Sampler::create(device, samplerConfig);
        if (frameLayout == nullptr || materialLayout == nullptr || sampleLayout == nullptr ||
            compositeLayout == nullptr || pool == nullptr || sampler == nullptr) {
            return false;
        }

        const glm::mat4 view = glm::lookAt(glm::vec3(3.6F, 2.8F, 5.2F),
                                           glm::vec3(0.0F, 0.45F, 0.0F),
                                           glm::vec3(0.0F, 1.0F, 0.0F));
        const glm::mat4 projection =
            glm::perspective(glm::radians(52.0F),
                             static_cast<float>(hdrWidth) / static_cast<float>(hdrHeight),
                             0.1F, 100.0F);
        glm::mat4 clipFix(1.0F);
        clipFix[2][2] = 0.5F;
        clipFix[3][2] = 0.5F;
        const glm::mat4 viewProjection = clipFix * projection * view;
        const glm::vec3 lightForward =
            glm::normalize(glm::vec3(-0.45F, -1.0F, -0.35F));
        const glm::vec3 lightEye = -lightForward * 14.0F;
        const glm::mat4 lightView = glm::lookAt(lightEye, glm::vec3(0.0F),
                                                glm::vec3(0.0F, 1.0F, 0.0F));
        const glm::mat4 lightProjection = glm::ortho(-7.0F, 7.0F, -7.0F, 7.0F,
                                                     1.0F, 30.0F);
        const glm::mat4 lightSpace = clipFix * lightProjection * lightView;
        const FrameBlock frameBlock = makeFrame(viewProjection);
        const FrameBlock shadowFrame = makeFrame(lightSpace);
        const LightBlock initialLights = makeHdrLights(lightSpace, true, true, true,
                                                       LightSelection{});
        const PbrBlock pbrBlock{Vec4{0.72F, 0.34F, 0.18F, 0.18F},
                                Vec4{0.0F, 0.0F, 0.0F, 0.38F},
                                Vec4{1.0F, 0.0F, 0.0F, 0.0F}};
        const PhongBlock phongBlock{Vec4{0.38F, 0.74F, 1.0F, 0.72F},
                                    Vec4{0.0F, 0.0F, 0.0F, 64.0F}};
        const PbrBlock emissiveBlock{Vec4{0.12F, 0.08F, 0.04F, 0.0F},
                                     Vec4{4.0F, 2.1F, 0.55F, 0.5F},
                                     Vec4{1.0F, 0.0F, 0.0F, 0.0F}};
        const WaterBlock waterBlock{0.1F,
                                    0.42F,
                                    0.48F,
                                    0.9F,
                                    {2.8F, 2.1F},
                                    1,
                                    {},
                                    Vec4{0.015F, 0.08F, 0.16F, 0.72F},
                                    Vec4{0.06F, 0.35F, 0.42F, 0.42F}};
        auto frameBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(frameBlock),
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       rb::vulkan::MemoryClass::hostUpload);
        auto shadowFrameBuffer = rb::vulkan::Buffer::create(
            device, *allocator, sizeof(shadowFrame), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        auto lightBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(LightBlock),
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       rb::vulkan::MemoryClass::hostUpload);
        auto pbrBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(pbrBlock),
                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                     rb::vulkan::MemoryClass::hostUpload);
        auto phongBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(phongBlock),
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       rb::vulkan::MemoryClass::hostUpload);
        auto emissiveBuffer = rb::vulkan::Buffer::create(
            device, *allocator, sizeof(emissiveBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            rb::vulkan::MemoryClass::hostUpload);
        auto waterBuffer = rb::vulkan::Buffer::create(device, *allocator, sizeof(waterBlock),
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       rb::vulkan::MemoryClass::hostUpload);
        if (frameBuffer == nullptr || shadowFrameBuffer == nullptr || lightBuffer == nullptr ||
            pbrBuffer == nullptr || phongBuffer == nullptr || emissiveBuffer == nullptr ||
            waterBuffer == nullptr || !updateBuffer(*frameBuffer, frameBlock) ||
            !updateBuffer(*shadowFrameBuffer, shadowFrame) ||
            !updateBuffer(*lightBuffer, initialLights) ||
            !updateBuffer(*pbrBuffer, pbrBlock) || !updateBuffer(*phongBuffer, phongBlock) ||
            !updateBuffer(*emissiveBuffer, emissiveBlock) ||
            !updateBuffer(*waterBuffer, waterBlock)) {
            return false;
        }

        const VkDescriptorSet frameSet = pool->allocate(frameLayout->handle());
        const VkDescriptorSet shadowFrameSet = pool->allocate(frameLayout->handle());
        const VkDescriptorSet pbrSet = pool->allocate(materialLayout->handle());
        const VkDescriptorSet phongSet = pool->allocate(materialLayout->handle());
        const VkDescriptorSet emissiveSet = pool->allocate(materialLayout->handle());
        const VkDescriptorSet waterSet = pool->allocate(materialLayout->handle());
        const VkDescriptorSet skySet = pool->allocate(sampleLayout->handle());
        const VkDescriptorSet convolveSet = pool->allocate(sampleLayout->handle());
        if (frameSet == VK_NULL_HANDLE || shadowFrameSet == VK_NULL_HANDLE ||
            pbrSet == VK_NULL_HANDLE || phongSet == VK_NULL_HANDLE ||
            emissiveSet == VK_NULL_HANDLE || waterSet == VK_NULL_HANDLE ||
            skySet == VK_NULL_HANDLE || convolveSet == VK_NULL_HANDLE) {
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 7> bufferInfos{
            VkDescriptorBufferInfo{frameBuffer->handle(), 0, sizeof(frameBlock)},
            VkDescriptorBufferInfo{shadowFrameBuffer->handle(), 0, sizeof(shadowFrame)},
            VkDescriptorBufferInfo{lightBuffer->handle(), 0, sizeof(LightBlock)},
            VkDescriptorBufferInfo{pbrBuffer->handle(), 0, sizeof(pbrBlock)},
            VkDescriptorBufferInfo{phongBuffer->handle(), 0, sizeof(phongBlock)},
            VkDescriptorBufferInfo{emissiveBuffer->handle(), 0, sizeof(emissiveBlock)},
            VkDescriptorBufferInfo{waterBuffer->handle(), 0, sizeof(waterBlock)}};
        const std::array<VkDescriptorImageInfo, 4> imageInfos{
            VkDescriptorImageInfo{sampler->handle(), irradiance->view(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            VkDescriptorImageInfo{sampler->handle(), shadow->depth()->view(),
                                  VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL},
            VkDescriptorImageInfo{sampler->handle(), albedo->view(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            VkDescriptorImageInfo{sampler->handle(), sky->view(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        std::array<VkWriteDescriptorSet, 16> writes{};
        writeBufferDescriptor(frameSet, 0, bufferInfos[0], writes[0]);
        writeBufferDescriptor(frameSet, 1, bufferInfos[2], writes[1]);
        writeImageDescriptor(frameSet, 2, imageInfos[0], writes[2]);
        writeImageDescriptor(frameSet, 3, imageInfos[1], writes[3]);
        writeBufferDescriptor(shadowFrameSet, 0, bufferInfos[1], writes[4]);
        writeBufferDescriptor(shadowFrameSet, 1, bufferInfos[2], writes[5]);
        writeImageDescriptor(shadowFrameSet, 2, imageInfos[0], writes[6]);
        writeImageDescriptor(shadowFrameSet, 3, imageInfos[1], writes[7]);
        writeBufferDescriptor(pbrSet, 0, bufferInfos[3], writes[8]);
        writeImageDescriptor(pbrSet, 1, imageInfos[2], writes[9]);
        writeBufferDescriptor(phongSet, 0, bufferInfos[4], writes[10]);
        writeImageDescriptor(phongSet, 1, imageInfos[2], writes[11]);
        writeBufferDescriptor(emissiveSet, 0, bufferInfos[5], writes[12]);
        writeImageDescriptor(emissiveSet, 1, imageInfos[2], writes[13]);
        writeBufferDescriptor(waterSet, 0, bufferInfos[6], writes[14]);
        writeImageDescriptor(waterSet, 1, imageInfos[3], writes[15]);
        vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
        const std::array<VkDescriptorImageInfo, 2> cubeInfos{imageInfos[3], imageInfos[3]};
        std::array<VkWriteDescriptorSet, 2> cubeWrites{};
        writeImageDescriptor(skySet, 0, cubeInfos[0], cubeWrites[0]);
        writeImageDescriptor(convolveSet, 0, cubeInfos[1], cubeWrites[1]);
        vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(cubeWrites.size()),
                               cubeWrites.data(), 0, nullptr);

        const std::array<rb::vulkan::VertexAttribute, 3> attributes{
            rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT,
                                        static_cast<std::uint32_t>(offsetof(rb::Vertex, position))},
            rb::vulkan::VertexAttribute{1, VK_FORMAT_R32G32B32_SFLOAT,
                                        static_cast<std::uint32_t>(offsetof(rb::Vertex, normal))},
            rb::vulkan::VertexAttribute{2, VK_FORMAT_R32G32_SFLOAT,
                                        static_cast<std::uint32_t>(offsetof(rb::Vertex, uv))}};
        const std::array<rb::vulkan::VertexAttribute, 1> positionAttribute{
            rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT,
                                        static_cast<std::uint32_t>(offsetof(rb::Vertex,
                                                                           position))}};
        const std::array<VkFormat, 1> rgba16Format{VK_FORMAT_R16G16B16A16_SFLOAT};
        const std::array<VkDescriptorSetLayout, 1> sampleSetLayout{sampleLayout->handle()};
        rb::vulkan::PipelineDescription convolvePipelineDescription{};
        convolvePipelineDescription.vertexCode = shaders.convolveVertex;
        convolvePipelineDescription.fragmentCode = shaders.convolveFragment;
        convolvePipelineDescription.vertexStride = sizeof(rb::Vertex);
        convolvePipelineDescription.vertexAttributes = positionAttribute;
        convolvePipelineDescription.colorFormats = rgba16Format;
        convolvePipelineDescription.setLayouts = sampleSetLayout;
        convolvePipelineDescription.pushConstantBytes = sizeof(ConvolvePush);
        convolvePipelineDescription.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        auto convolvePipeline = rb::vulkan::Pipeline::create(
            device, convolvePipelineDescription, VK_NULL_HANDLE);
        if (convolvePipeline == nullptr) {
            return false;
        }

        if (!commands.begin()) {
            return false;
        }
        rb::vulkan::ImageBarrier irradianceAttachment{};
        irradianceAttachment.image = irradiance->handle();
        irradianceAttachment.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        irradianceAttachment.dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        irradianceAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        irradianceAttachment.arrayLayers = 6U;
        rb::vulkan::cmdImageBarrier(commands.buffer, irradianceAttachment);
        const glm::vec3 origin{0.0F};
        const std::array<glm::mat4, 6> views{
            glm::lookAt(origin, glm::vec3(1.0F, 0.0F, 0.0F),
                        glm::vec3(0.0F, -1.0F, 0.0F)),
            glm::lookAt(origin, glm::vec3(-1.0F, 0.0F, 0.0F),
                        glm::vec3(0.0F, -1.0F, 0.0F)),
            glm::lookAt(origin, glm::vec3(0.0F, 1.0F, 0.0F),
                        glm::vec3(0.0F, 0.0F, 1.0F)),
            glm::lookAt(origin, glm::vec3(0.0F, -1.0F, 0.0F),
                        glm::vec3(0.0F, 0.0F, -1.0F)),
            glm::lookAt(origin, glm::vec3(0.0F, 0.0F, 1.0F),
                        glm::vec3(0.0F, -1.0F, 0.0F)),
            glm::lookAt(origin, glm::vec3(0.0F, 0.0F, -1.0F),
                        glm::vec3(0.0F, -1.0F, 0.0F))};
        const glm::mat4 cubeProjection =
            clipFix * glm::perspective(glm::radians(90.0F), 1.0F, 0.1F, 10.0F);
        const VkBuffer vertexHandle = vertexBuffer->handle();
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
        vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          convolvePipeline->handle());
        vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                convolvePipeline->layout(), 0, 1, &convolveSet, 0, nullptr);
        for (std::uint32_t face = 0; face < 6U; ++face) {
            beginRendering(commands.buffer, irradiance->extent(), irradiance->layerView(face),
                           VK_NULL_HANDLE, {0.0F, 0.0F, 0.0F, 1.0F});
            const ConvolvePush push = makeConvolvePush(views[face], cubeProjection);
            vkCmdPushConstants(commands.buffer, convolvePipeline->layout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(commands.buffer, cubeRange.indexCount, 1, cubeRange.firstIndex,
                             cubeRange.vertexOffset, 0);
            vkCmdEndRendering(commands.buffer);
        }
        rb::vulkan::ImageBarrier irradianceSample{};
        irradianceSample.image = irradiance->handle();
        irradianceSample.srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        irradianceSample.srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        irradianceSample.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        irradianceSample.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        irradianceSample.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        irradianceSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradianceSample.arrayLayers = 6U;
        rb::vulkan::cmdImageBarrier(commands.buffer, irradianceSample);
        if (!commands.submitAndWait(device.graphicsQueue())) {
            return false;
        }
        auto readback = rb::vulkan::Readback::create(device, *allocator);
        if (readback == nullptr) {
            return false;
        }
        HdrChecks checks;
        checks.irradiance = checkIrradianceFaces(*readback, *irradiance);

        const std::array<VkDescriptorSetLayout, 2> sceneSetLayouts{
            frameLayout->handle(), materialLayout->handle()};
        const auto makeScenePipeline = [&](std::span<const std::uint32_t> vertexCode,
                                           std::span<const std::uint32_t> fragmentCode,
                                           VkFormat colorFormat,
                                           rb::vulkan::BlendMode blendMode,
                                           bool depthWrite,
                                           std::uint32_t pushBytes,
                                           VkShaderStageFlags pushStages) {
            const std::array<VkFormat, 1> formats{colorFormat};
            rb::vulkan::PipelineDescription description{};
            description.vertexCode = vertexCode;
            description.fragmentCode = fragmentCode;
            description.vertexStride = sizeof(rb::Vertex);
            description.vertexAttributes = pushBytes == sizeof(PushBlock)
                                               ? std::span<const rb::vulkan::VertexAttribute>(
                                                     attributes)
                                               : std::span<const rb::vulkan::VertexAttribute>(
                                                     positionAttribute);
            description.cullMode = VK_CULL_MODE_NONE;
            description.blendMode = blendMode;
            description.depthTest = true;
            description.depthWrite = depthWrite;
            description.colorFormats = formats;
            description.depthFormat = VK_FORMAT_D32_SFLOAT;
            description.setLayouts = sceneSetLayouts;
            description.pushConstantBytes = pushBytes;
            description.pushConstantStages = pushStages;
            return rb::vulkan::Pipeline::create(device, description, VK_NULL_HANDLE);
        };
        const auto makeSkyboxPipeline = [&](VkFormat colorFormat) {
            const std::array<VkFormat, 1> formats{colorFormat};
            rb::vulkan::PipelineDescription description{};
            description.vertexCode = shaders.skyboxVertex;
            description.fragmentCode = shaders.skyboxFragment;
            description.vertexStride = sizeof(rb::Vertex);
            description.vertexAttributes = positionAttribute;
            description.depthTest = true;
            description.depthWrite = false;
            description.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
            description.colorFormats = formats;
            description.depthFormat = VK_FORMAT_D32_SFLOAT;
            description.setLayouts = sampleSetLayout;
            description.pushConstantBytes = sizeof(SkyboxPush);
            description.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT |
                                             VK_SHADER_STAGE_FRAGMENT_BIT;
            return rb::vulkan::Pipeline::create(device, description, VK_NULL_HANDLE);
        };
        auto pbrHdrPipeline = makeScenePipeline(
            shaders.litVertex, shaders.pbrFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            rb::vulkan::BlendMode::opaque, true, sizeof(PushBlock),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto phongHdrPipeline = makeScenePipeline(
            shaders.litVertex, shaders.phongFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            rb::vulkan::BlendMode::opaque, true, sizeof(PushBlock),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto waterHdrPipeline = makeScenePipeline(
            shaders.waterVertex, shaders.waterFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            rb::vulkan::BlendMode::alphaBlend, false, sizeof(ModelPush),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto skyHdrPipeline = makeSkyboxPipeline(VK_FORMAT_R16G16B16A16_SFLOAT);
        auto pbrDirectPipeline = makeScenePipeline(
            shaders.litVertex, shaders.pbrFragment, VK_FORMAT_R8G8B8A8_UNORM,
            rb::vulkan::BlendMode::opaque, true, sizeof(PushBlock),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto phongDirectPipeline = makeScenePipeline(
            shaders.litVertex, shaders.phongFragment, VK_FORMAT_R8G8B8A8_UNORM,
            rb::vulkan::BlendMode::opaque, true, sizeof(PushBlock),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto waterDirectPipeline = makeScenePipeline(
            shaders.waterVertex, shaders.waterFragment, VK_FORMAT_R8G8B8A8_UNORM,
            rb::vulkan::BlendMode::alphaBlend, false, sizeof(ModelPush),
            VK_SHADER_STAGE_VERTEX_BIT);
        auto skyDirectPipeline = makeSkyboxPipeline(VK_FORMAT_R8G8B8A8_UNORM);
        rb::vulkan::PipelineDescription depthPipelineDescription{};
        depthPipelineDescription.vertexCode = shaders.depthVertex;
        depthPipelineDescription.fragmentCode = shaders.depthFragment;
        depthPipelineDescription.vertexStride = sizeof(rb::Vertex);
        depthPipelineDescription.vertexAttributes = positionAttribute;
        depthPipelineDescription.depthTest = true;
        depthPipelineDescription.depthWrite = true;
        depthPipelineDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
        depthPipelineDescription.setLayouts = sampleSetLayout;
        const std::array<VkDescriptorSetLayout, 1> shadowSetLayout{frameLayout->handle()};
        depthPipelineDescription.setLayouts = shadowSetLayout;
        depthPipelineDescription.pushConstantBytes = sizeof(ModelPush);
        depthPipelineDescription.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        auto depthPipeline = rb::vulkan::Pipeline::create(device, depthPipelineDescription,
                                                           VK_NULL_HANDLE);
        if (pbrHdrPipeline == nullptr || phongHdrPipeline == nullptr ||
            waterHdrPipeline == nullptr || skyHdrPipeline == nullptr ||
            pbrDirectPipeline == nullptr || phongDirectPipeline == nullptr ||
            waterDirectPipeline == nullptr || skyDirectPipeline == nullptr ||
            depthPipeline == nullptr) {
            return false;
        }

        const glm::mat4 floorModel =
            glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, -0.05F, 0.0F)) *
            glm::rotate(glm::mat4(1.0F), glm::radians(-90.0F),
                        glm::vec3(1.0F, 0.0F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(9.0F, 9.0F, 1.0F));
        const glm::mat4 pbrModel =
            glm::translate(glm::mat4(1.0F), glm::vec3(-1.1F, 0.65F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(1.3F));
        const glm::mat4 phongModel =
            glm::translate(glm::mat4(1.0F), glm::vec3(0.85F, 0.55F, -0.45F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(1.1F));
        const glm::mat4 emissiveModel =
            glm::translate(glm::mat4(1.0F), glm::vec3(0.25F, 1.25F, 0.9F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(0.42F));
        const glm::mat4 waterModel =
            glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.08F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(2.8F, 1.0F, 2.1F));
        const std::array<std::pair<const MeshRange*, const glm::mat4*>, 4> shadowDraws{{
            {&planeRange, &floorModel}, {&sphereRange, &pbrModel},
            {&cubeRange, &phongModel}, {&cubeRange, &emissiveModel}}};

        if (!commands.begin()) {
            return false;
        }
        transitionDepthToAttachment(commands.buffer, *shadow->depth(),
                                    VK_IMAGE_LAYOUT_UNDEFINED);
        beginRendering(commands.buffer, shadow->extent(), VK_NULL_HANDLE,
                       shadow->depth()->view(), {0.0F, 0.0F, 0.0F, 0.0F});
        vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          depthPipeline->handle());
        vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                depthPipeline->layout(), 0, 1, &shadowFrameSet, 0, nullptr);
        vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
        vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        for (const auto& [range, model] : shadowDraws) {
            const ModelPush push = makeModelPush(*model);
            vkCmdPushConstants(commands.buffer, depthPipeline->layout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(commands.buffer, range->indexCount, 1, range->firstIndex,
                             range->vertexOffset, 0);
        }
        vkCmdEndRendering(commands.buffer);
        rb::vulkan::ImageBarrier shadowSample{};
        shadowSample.image = shadow->depth()->handle();
        shadowSample.srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        shadowSample.srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadowSample.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        shadowSample.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        shadowSample.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        shadowSample.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        shadowSample.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        rb::vulkan::cmdImageBarrier(commands.buffer, shadowSample);
        if (!commands.submitAndWait(device.graphicsQueue())) {
            return false;
        }
        std::vector<std::byte> shadowPixels(static_cast<std::size_t>(shadowSize) *
                                            shadowSize * sizeof(float));
        checks.shadows = shadow->readDepth(*readback, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, shadowPixels) &&
                         checkDepthPixels(shadowPixels);

        bool hdrInitialized = false;
        bool directInitialized = false;
        const glm::mat4 skyViewProjection =
            clipFix * projection * glm::mat4(glm::mat3(view));
        const auto renderScene = [&](rb::vulkan::OffscreenTarget& target,
                                     bool& initialized, bool hdrOutput,
                                     bool environmentEnabled, bool shadowEnabled,
                                     bool drawSkybox, bool waterSkybox,
                                     std::vector<std::byte>& pixels,
                                     const LightSelection& lightSelection = LightSelection{}) {
            const LightBlock lights = makeHdrLights(lightSpace, environmentEnabled,
                                                    shadowEnabled, hdrOutput, lightSelection);
            WaterBlock waterValues = waterBlock;
            waterValues.hasSkybox = waterSkybox ? 1 : 0;
            if (!updateBuffer(*lightBuffer, lights) || !updateBuffer(*waterBuffer, waterValues) ||
                !commands.begin()) {
                return false;
            }
            transitionColorToAttachment(
                commands.buffer, *target.color(),
                initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
                initialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_2_NONE,
                initialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE);
            transitionDepthToAttachment(
                commands.buffer, *target.depth(),
                initialized ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
                initialized ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                            : VK_PIPELINE_STAGE_2_NONE,
                initialized ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                            : VK_ACCESS_2_NONE);
            beginRendering(commands.buffer, target.extent(), target.color()->view(),
                           target.depth()->view(), clearColor);
            vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
            vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0,
                                 VK_INDEX_TYPE_UINT32);
            const auto& pbrPipeline = hdrOutput ? pbrHdrPipeline : pbrDirectPipeline;
            const auto& phongPipeline = hdrOutput ? phongHdrPipeline : phongDirectPipeline;
            const auto& skyPipeline = hdrOutput ? skyHdrPipeline : skyDirectPipeline;
            const auto& waterPipeline = hdrOutput ? waterHdrPipeline : waterDirectPipeline;
            if (drawSkybox) {
                vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  skyPipeline->handle());
                vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        skyPipeline->layout(), 0, 1, &skySet, 0, nullptr);
                const SkyboxPush skyPush = makeSkyboxPush(skyViewProjection, hdrOutput);
                vkCmdPushConstants(commands.buffer, skyPipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(skyPush), &skyPush);
                vkCmdDrawIndexed(commands.buffer, cubeRange.indexCount, 1,
                                 cubeRange.firstIndex, cubeRange.vertexOffset, 0);
            }
            const auto drawMaterial = [&](const rb::vulkan::Pipeline& pipeline,
                                          VkDescriptorSet materialSet,
                                          const MeshRange& range,
                                          const glm::mat4& model) {
                vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline.handle());
                const std::array<VkDescriptorSet, 2> sets{frameSet, materialSet};
                vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline.layout(), 0,
                                        static_cast<std::uint32_t>(sets.size()), sets.data(), 0,
                                        nullptr);
                const PushBlock push = makePush(model);
                vkCmdPushConstants(commands.buffer, pipeline.layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(commands.buffer, range.indexCount, 1, range.firstIndex,
                                 range.vertexOffset, 0);
            };
            drawMaterial(*pbrPipeline, pbrSet, planeRange, floorModel);
            drawMaterial(*pbrPipeline, pbrSet, sphereRange, pbrModel);
            drawMaterial(*phongPipeline, phongSet, cubeRange, phongModel);
            drawMaterial(*pbrPipeline, emissiveSet, cubeRange, emissiveModel);
            vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              waterPipeline->handle());
            const std::array<VkDescriptorSet, 2> waterSets{frameSet, waterSet};
            vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    waterPipeline->layout(), 0,
                                    static_cast<std::uint32_t>(waterSets.size()),
                                    waterSets.data(), 0, nullptr);
            const ModelPush waterPush = makeModelPush(waterModel);
            vkCmdPushConstants(commands.buffer, waterPipeline->layout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(waterPush), &waterPush);
            vkCmdDrawIndexed(commands.buffer, waterRange.indexCount, 1, waterRange.firstIndex,
                             waterRange.vertexOffset, 0);
            vkCmdEndRendering(commands.buffer);
            transitionColorToSample(commands.buffer, *target.color());
            if (!commands.submitAndWait(device.graphicsQueue())) {
                return false;
            }
            initialized = true;
            const std::size_t texelBytes = rb::vulkan::formatTexelBytes(target.color()->format());
            pixels.resize(static_cast<std::size_t>(target.extent().width) *
                          target.extent().height * texelBytes);
            return target.readColor(*readback, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, pixels);
        };

        std::vector<std::byte> directPixels;
        if (!renderScene(*directTarget, directInitialized, false, false, true, true, false,
                         directPixels)) {
            return false;
        }
        const LdrSceneChecks directChecks = checkLdrScene(directPixels, viewProjection);
        checks.direct = directChecks.materials;
        checks.emissive = directChecks.emissive;

        std::vector<std::byte> noSkyPixels;
        if (!renderScene(*directTarget, directInitialized, false, false, true, false, false,
                         noSkyPixels)) {
            return false;
        }
        unsigned skyMaximum = 0;
        const double skyMean = byteMeanDifference(directPixels, noSkyPixels, skyMaximum);
        checks.skybox = skyMaximum > 0U && skyMean > 0.01;
        std::cout << "Vulkan skybox difference max=" << skyMaximum
                  << " mean=" << skyMean
                  << " status=" << (checks.skybox ? "pass" : "fail") << '\n';

        const std::array<LightSelection, 3> disabledLights{{
            LightSelection{false, true, true}, LightSelection{true, false, true},
            LightSelection{true, true, false}}};
        constexpr std::array<const char*, 3> lightNames{"directional", "point", "spot"};
        checks.lights = true;
        for (std::size_t index = 0; index < disabledLights.size(); ++index) {
            std::vector<std::byte> lightPixels;
            if (!renderScene(*directTarget, directInitialized, false, false, true, true, false,
                             lightPixels, disabledLights[index])) {
                return false;
            }
            unsigned maximum = 0;
            const double mean = byteMeanDifference(directPixels, lightPixels, maximum);
            const bool passed = maximum > 0U && mean > 0.001;
            checks.lights = checks.lights && passed;
            std::cout << "Vulkan " << lightNames[index] << " light difference max="
                      << maximum << " mean=" << mean
                      << " status=" << (passed ? "pass" : "fail") << '\n';
        }

        std::vector<std::byte> environmentPixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, true, true, true, true,
                         environmentPixels)) {
            return false;
        }
        checks.hdr = checkHdrPixels(environmentPixels, "scene");
        std::vector<std::byte> noShadowPixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, true, false, true, true,
                         noShadowPixels)) {
            return false;
        }
        unsigned shadowMaximum = 0;
        const double shadowMean = byteMeanDifference(
            environmentPixels, noShadowPixels, shadowMaximum);
        checks.shadows = checks.shadows && shadowMaximum > 0U && shadowMean > 0.001;
        std::cout << "Vulkan shadow difference max=" << shadowMaximum
                  << " mean=" << shadowMean
                  << " status=" << (checks.shadows ? "pass" : "fail") << '\n';
        std::vector<std::byte> noEnvironmentPixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, false, true, true, true,
                         noEnvironmentPixels)) {
            return false;
        }
        unsigned environmentMaximum = 0;
        const double environmentMean = byteMeanDifference(
            environmentPixels, noEnvironmentPixels, environmentMaximum);
        checks.environment = environmentMaximum > 0U && environmentMean > 0.01;
        std::cout << "Vulkan environment difference max=" << environmentMaximum
                  << " mean=" << environmentMean
                  << " status=" << (checks.environment ? "pass" : "fail") << '\n';

        std::vector<std::byte> noWaterSkyPixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, false, true, true, false,
                         noWaterSkyPixels)) {
            return false;
        }
        unsigned waterMaximum = 0;
        const double waterMean = byteMeanDifference(noEnvironmentPixels, noWaterSkyPixels,
                                                    waterMaximum);
        checks.water = waterMaximum > 0U && waterMean > 0.001;
        std::cout << "Vulkan water sky difference max=" << waterMaximum
                  << " mean=" << waterMean
                  << " status=" << (checks.water ? "pass" : "fail") << '\n';

        const VkDescriptorImageInfo fallbackInfo{sampler->handle(), sky->view(),
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet fallbackWrite{};
        writeImageDescriptor(frameSet, 2, fallbackInfo, fallbackWrite);
        vkUpdateDescriptorSets(device.handle(), 1, &fallbackWrite, 0, nullptr);
        std::vector<std::byte> fallbackPixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, false, true, true, false,
                         fallbackPixels)) {
            return false;
        }
        unsigned fallbackMaximum = 0;
        const double fallbackMean = byteMeanDifference(noWaterSkyPixels, fallbackPixels,
                                                       fallbackMaximum);
        checks.fallback = fallbackMaximum == 0U && fallbackMean == 0.0;
        std::cout << "Vulkan environment fallback max=" << fallbackMaximum
                  << " mean=" << fallbackMean
                  << " status=" << (checks.fallback ? "pass" : "fail") << '\n';

        const VkDescriptorImageInfo environmentInfo{
            sampler->handle(), irradiance->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet environmentWrite{};
        writeImageDescriptor(frameSet, 2, environmentInfo, environmentWrite);
        vkUpdateDescriptorSets(device.handle(), 1, &environmentWrite, 0, nullptr);
        std::vector<std::byte> postSourcePixels;
        if (!renderScene(*hdrTarget, hdrInitialized, true, true, true, true, true,
                         postSourcePixels)) {
            return false;
        }
        checks.hdr = checkHdrPixels(postSourcePixels, "post source") && checks.hdr;

        const auto makePostPipeline = [&](std::span<const std::uint32_t> fragmentCode,
                                          VkFormat colorFormat,
                                          VkDescriptorSetLayout setLayout,
                                          std::uint32_t pushBytes,
                                          rb::vulkan::BlendMode blendMode) {
            const std::array<VkFormat, 1> formats{colorFormat};
            const std::array<VkDescriptorSetLayout, 1> layouts{setLayout};
            rb::vulkan::PipelineDescription description{};
            description.vertexCode = shaders.fullscreenVertex;
            description.fragmentCode = fragmentCode;
            description.blendMode = blendMode;
            description.colorFormats = formats;
            description.setLayouts = layouts;
            description.pushConstantBytes = pushBytes;
            description.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
            return rb::vulkan::Pipeline::create(device, description, VK_NULL_HANDLE);
        };
        auto prefilterPipeline = makePostPipeline(
            shaders.prefilterFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            sampleLayout->handle(), sizeof(PrefilterPush), rb::vulkan::BlendMode::opaque);
        auto downsamplePipeline = makePostPipeline(
            shaders.downsampleFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            sampleLayout->handle(), sizeof(TexelPush), rb::vulkan::BlendMode::opaque);
        auto upsamplePipeline = makePostPipeline(
            shaders.upsampleFragment, VK_FORMAT_R16G16B16A16_SFLOAT,
            sampleLayout->handle(), sizeof(UpsamplePush), rb::vulkan::BlendMode::additive);
        auto compositePipeline = makePostPipeline(
            shaders.compositeFragment, VK_FORMAT_R8G8B8A8_UNORM,
            compositeLayout->handle(), sizeof(CompositePush), rb::vulkan::BlendMode::opaque);
        auto fxaaPipeline = makePostPipeline(
            shaders.fxaaFragment, VK_FORMAT_R8G8B8A8_UNORM,
            sampleLayout->handle(), sizeof(TexelPush), rb::vulkan::BlendMode::opaque);
        if (prefilterPipeline == nullptr || downsamplePipeline == nullptr ||
            upsamplePipeline == nullptr || compositePipeline == nullptr ||
            fxaaPipeline == nullptr) {
            return false;
        }

        const auto allocateSampleSet = [&](rb::vulkan::DescriptorPool& descriptorPool,
                                           VkImageView view) {
            const VkDescriptorSet set = descriptorPool.allocate(sampleLayout->handle());
            if (set != VK_NULL_HANDLE) {
                const VkDescriptorImageInfo info{sampler->handle(), view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet write{};
                writeImageDescriptor(set, 0, info, write);
                vkUpdateDescriptorSets(device.handle(), 1, &write, 0, nullptr);
            }
            return set;
        };
        const auto allocateCompositeSet = [&](rb::vulkan::DescriptorPool& descriptorPool,
                                              VkImageView sceneView, VkImageView bloomView) {
            const VkDescriptorSet set = descriptorPool.allocate(compositeLayout->handle());
            if (set != VK_NULL_HANDLE) {
                const std::array<VkDescriptorImageInfo, 2> infos{
                    VkDescriptorImageInfo{sampler->handle(), sceneView,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    VkDescriptorImageInfo{sampler->handle(), bloomView,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
                std::array<VkWriteDescriptorSet, 2> setWrites{};
                writeImageDescriptor(set, 0, infos[0], setWrites[0]);
                writeImageDescriptor(set, 1, infos[1], setWrites[1]);
                vkUpdateDescriptorSets(device.handle(),
                                       static_cast<std::uint32_t>(setWrites.size()),
                                       setWrites.data(), 0, nullptr);
            }
            return set;
        };
        const auto createColorTarget = [&](VkExtent2D extent, VkFormat format) {
            rb::vulkan::OffscreenTargetDescription description{};
            description.extent = extent;
            description.colorFormat = format;
            description.sampledColor = true;
            return rb::vulkan::OffscreenTarget::create(device, *allocator, description);
        };
        const auto processPost = [&](VkExtent2D outputExtent, bool bloomEnabled,
                                     bool fxaaEnabled,
                                     const CompositePush& compositeSettings,
                                     std::vector<std::byte>& pixels,
                                     std::size_t& bloomLevels) {
            std::vector<std::unique_ptr<rb::vulkan::OffscreenTarget>> bloomTargets;
            std::uint32_t width = std::max(outputExtent.width / 2U, 1U);
            std::uint32_t height = std::max(outputExtent.height / 2U, 1U);
            constexpr std::size_t maximumLevels = 6U;
            for (std::size_t level = 0; level < maximumLevels; ++level) {
                auto target = createColorTarget({width, height},
                                                VK_FORMAT_R16G16B16A16_SFLOAT);
                if (target == nullptr || target->color() == nullptr) {
                    return false;
                }
                bloomTargets.push_back(std::move(target));
                if (width <= 2U || height <= 2U) {
                    break;
                }
                width = std::max(width / 2U, 1U);
                height = std::max(height / 2U, 1U);
            }
            bloomLevels = bloomTargets.size();
            auto ldr = createColorTarget(outputExtent, VK_FORMAT_R8G8B8A8_UNORM);
            auto aa = createColorTarget(outputExtent, VK_FORMAT_R8G8B8A8_UNORM);
            if (ldr == nullptr || ldr->color() == nullptr || aa == nullptr ||
                aa->color() == nullptr) {
                return false;
            }
            const std::array<VkDescriptorPoolSize, 1> postPoolSizes{
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16U}};
            auto postPool = rb::vulkan::DescriptorPool::create(device, 16U,
                                                               postPoolSizes);
            if (postPool == nullptr) {
                return false;
            }

            std::vector<VkDescriptorSet> downsampleSets;
            std::vector<VkDescriptorSet> upsampleSets;
            const VkDescriptorSet prefilterSet =
                allocateSampleSet(*postPool, hdrTarget->color()->view());
            if (prefilterSet == VK_NULL_HANDLE) {
                return false;
            }
            for (std::size_t level = 0; level + 1U < bloomTargets.size(); ++level) {
                downsampleSets.push_back(
                    allocateSampleSet(*postPool, bloomTargets[level]->color()->view()));
            }
            for (std::size_t level = bloomTargets.size(); level > 1U; --level) {
                upsampleSets.push_back(
                    allocateSampleSet(*postPool, bloomTargets[level - 1U]->color()->view()));
            }
            const VkImageView bloomView = bloomEnabled
                                              ? bloomTargets.front()->color()->view()
                                              : hdrTarget->color()->view();
            const VkDescriptorSet compositeSet =
                allocateCompositeSet(*postPool, hdrTarget->color()->view(), bloomView);
            const VkDescriptorSet fxaaSet =
                allocateSampleSet(*postPool, ldr->color()->view());
            if (std::find(downsampleSets.begin(), downsampleSets.end(), VK_NULL_HANDLE) !=
                    downsampleSets.end() ||
                std::find(upsampleSets.begin(), upsampleSets.end(), VK_NULL_HANDLE) !=
                    upsampleSets.end() ||
                compositeSet == VK_NULL_HANDLE || fxaaSet == VK_NULL_HANDLE ||
                !commands.begin()) {
                return false;
            }

            if (bloomEnabled) {
                transitionColorToAttachment(commands.buffer, *bloomTargets[0]->color(),
                                            VK_IMAGE_LAYOUT_UNDEFINED);
                beginRendering(commands.buffer, bloomTargets[0]->extent(),
                               bloomTargets[0]->color()->view(), VK_NULL_HANDLE,
                               {0.0F, 0.0F, 0.0F, 1.0F});
                vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  prefilterPipeline->handle());
                vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        prefilterPipeline->layout(), 0, 1, &prefilterSet, 0,
                                        nullptr);
                const PrefilterPush prefilterPush{};
                vkCmdPushConstants(commands.buffer, prefilterPipeline->layout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(prefilterPush),
                                   &prefilterPush);
                vkCmdDraw(commands.buffer, 3U, 1U, 0U, 0U);
                vkCmdEndRendering(commands.buffer);
                transitionColorToSample(commands.buffer, *bloomTargets[0]->color());

                for (std::size_t level = 0; level + 1U < bloomTargets.size(); ++level) {
                    transitionColorToAttachment(commands.buffer,
                                                *bloomTargets[level + 1U]->color(),
                                                VK_IMAGE_LAYOUT_UNDEFINED);
                    beginRendering(commands.buffer, bloomTargets[level + 1U]->extent(),
                                   bloomTargets[level + 1U]->color()->view(), VK_NULL_HANDLE,
                                   {0.0F, 0.0F, 0.0F, 1.0F});
                    vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      downsamplePipeline->handle());
                    const VkDescriptorSet set = downsampleSets[level];
                    vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            downsamplePipeline->layout(), 0, 1, &set, 0,
                                            nullptr);
                    const VkExtent2D sourceExtent = bloomTargets[level]->extent();
                    const TexelPush push{{1.0F / static_cast<float>(sourceExtent.width),
                                          1.0F / static_cast<float>(sourceExtent.height)}};
                    vkCmdPushConstants(commands.buffer, downsamplePipeline->layout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                    vkCmdDraw(commands.buffer, 3U, 1U, 0U, 0U);
                    vkCmdEndRendering(commands.buffer);
                    transitionColorToSample(commands.buffer,
                                            *bloomTargets[level + 1U]->color());
                }
                for (std::size_t source = bloomTargets.size(); source > 1U; --source) {
                    const std::size_t sourceIndex = source - 1U;
                    const std::size_t destinationIndex = sourceIndex - 1U;
                    transitionColorToAttachment(
                        commands.buffer, *bloomTargets[destinationIndex]->color(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                    beginRendering(commands.buffer, bloomTargets[destinationIndex]->extent(),
                                   bloomTargets[destinationIndex]->color()->view(),
                                   VK_NULL_HANDLE, {0.0F, 0.0F, 0.0F, 1.0F},
                                   VK_ATTACHMENT_LOAD_OP_LOAD);
                    vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      upsamplePipeline->handle());
                    const VkDescriptorSet set =
                        upsampleSets[bloomTargets.size() - source];
                    vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            upsamplePipeline->layout(), 0, 1, &set, 0,
                                            nullptr);
                    const VkExtent2D sourceExtent = bloomTargets[sourceIndex]->extent();
                    const UpsamplePush push{{1.0F / static_cast<float>(sourceExtent.width),
                                             1.0F / static_cast<float>(sourceExtent.height)},
                                            1.0F};
                    vkCmdPushConstants(commands.buffer, upsamplePipeline->layout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                    vkCmdDraw(commands.buffer, 3U, 1U, 0U, 0U);
                    vkCmdEndRendering(commands.buffer);
                    transitionColorToSample(commands.buffer,
                                            *bloomTargets[destinationIndex]->color());
                }
            }

            transitionColorToAttachment(commands.buffer, *ldr->color(),
                                        VK_IMAGE_LAYOUT_UNDEFINED);
            beginRendering(commands.buffer, outputExtent, ldr->color()->view(),
                           VK_NULL_HANDLE, {0.0F, 0.0F, 0.0F, 1.0F});
            vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              compositePipeline->handle());
            vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    compositePipeline->layout(), 0, 1, &compositeSet, 0,
                                    nullptr);
            CompositePush compositePush = compositeSettings;
            compositePush.bloomEnabled = bloomEnabled ? 1 : 0;
            vkCmdPushConstants(commands.buffer, compositePipeline->layout(),
                               VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(compositePush),
                               &compositePush);
            vkCmdDraw(commands.buffer, 3U, 1U, 0U, 0U);
            vkCmdEndRendering(commands.buffer);
            transitionColorToSample(commands.buffer, *ldr->color());

            rb::vulkan::OffscreenTarget* result = ldr.get();
            if (fxaaEnabled) {
                transitionColorToAttachment(commands.buffer, *aa->color(),
                                            VK_IMAGE_LAYOUT_UNDEFINED);
                beginRendering(commands.buffer, outputExtent, aa->color()->view(),
                               VK_NULL_HANDLE, {0.0F, 0.0F, 0.0F, 1.0F});
                vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  fxaaPipeline->handle());
                vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        fxaaPipeline->layout(), 0, 1, &fxaaSet, 0, nullptr);
                const TexelPush fxaaPush{{1.0F / static_cast<float>(outputExtent.width),
                                          1.0F / static_cast<float>(outputExtent.height)}};
                vkCmdPushConstants(commands.buffer, fxaaPipeline->layout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fxaaPush),
                                   &fxaaPush);
                vkCmdDraw(commands.buffer, 3U, 1U, 0U, 0U);
                vkCmdEndRendering(commands.buffer);
                transitionColorToSample(commands.buffer, *aa->color());
                result = aa.get();
            }
            if (!commands.submitAndWait(device.graphicsQueue())) {
                return false;
            }
            pixels.resize(static_cast<std::size_t>(outputExtent.width) *
                          outputExtent.height * 4U);
            return result->readColor(*readback, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, pixels);
        };

        std::vector<std::byte> postPixels;
        std::vector<std::byte> noBloomPixels;
        std::vector<std::byte> noFxaaPixels;
        std::vector<std::byte> reinhardPixels;
        std::vector<std::byte> neutralColorPixels;
        std::vector<std::byte> linearGammaPixels;
        std::size_t mainLevels = 0;
        std::size_t noBloomLevels = 0;
        std::size_t noFxaaLevels = 0;
        std::size_t variantLevels = 0;
        const CompositePush standardComposite{};
        CompositePush reinhardComposite = standardComposite;
        reinhardComposite.tonemap = 1;
        CompositePush neutralColorComposite = standardComposite;
        neutralColorComposite.exposure = 0.0F;
        neutralColorComposite.contrast = 1.0F;
        neutralColorComposite.saturation = 1.0F;
        neutralColorComposite.vignette = 0.0F;
        CompositePush linearGammaComposite = standardComposite;
        linearGammaComposite.gamma = 1.0F;
        if (!processPost({hdrWidth, hdrHeight}, true, true, standardComposite,
                         postPixels, mainLevels) ||
            !processPost({hdrWidth, hdrHeight}, false, true, standardComposite,
                         noBloomPixels, noBloomLevels) ||
            !processPost({hdrWidth, hdrHeight}, false, false, standardComposite,
                         noFxaaPixels, noFxaaLevels) ||
            !processPost({hdrWidth, hdrHeight}, false, false, reinhardComposite,
                         reinhardPixels, variantLevels) ||
            !processPost({hdrWidth, hdrHeight}, false, false, neutralColorComposite,
                         neutralColorPixels, variantLevels) ||
            !processPost({hdrWidth, hdrHeight}, false, false, linearGammaComposite,
                         linearGammaPixels, variantLevels)) {
            return false;
        }
        checks.post = postPixels.size() == static_cast<std::size_t>(hdrWidth) * hdrHeight * 4U;
        unsigned postMinimum = 255U;
        unsigned postMaximum = 0U;
        for (std::size_t index = 0; index < postPixels.size(); ++index) {
            const unsigned value = std::to_integer<unsigned>(postPixels[index]);
            if (index % 4U == 3U) {
                checks.post = checks.post && value == 255U;
            } else {
                postMinimum = std::min(postMinimum, value);
                postMaximum = std::max(postMaximum, value);
            }
        }
        checks.post = checks.post && postMaximum > postMinimum;
        unsigned bloomMaximum = 0;
        const double bloomMean = byteMeanDifference(postPixels, noBloomPixels, bloomMaximum);
        checks.bloom = bloomMaximum > 0U && bloomMean > 0.001;
        unsigned fxaaMaximum = 0;
        const double fxaaMean = byteMeanDifference(noBloomPixels, noFxaaPixels, fxaaMaximum);
        checks.fxaa = fxaaMaximum > 0U && fxaaMean > 0.001;
        unsigned toneMaximum = 0;
        const double toneMean = byteMeanDifference(noFxaaPixels, reinhardPixels, toneMaximum);
        checks.toneMapping = toneMaximum > 0U && toneMean > 0.01;
        unsigned colorMaximum = 0;
        const double colorMean = byteMeanDifference(noFxaaPixels, neutralColorPixels,
                                                    colorMaximum);
        checks.colorControls = colorMaximum > 0U && colorMean > 0.01;
        unsigned gammaMaximum = 0;
        const double gammaMean = byteMeanDifference(noFxaaPixels, linearGammaPixels,
                                                    gammaMaximum);
        checks.gamma = gammaMaximum > 0U && gammaMean > 0.01;
        std::cout << "Vulkan post range=" << postMinimum << ',' << postMaximum
                  << " bloom_max=" << bloomMaximum << " bloom_mean=" << bloomMean
                  << " fxaa_max=" << fxaaMaximum << " fxaa_mean=" << fxaaMean
                  << " tonemap_max=" << toneMaximum << " tonemap_mean=" << toneMean
                  << " color_max=" << colorMaximum << " color_mean=" << colorMean
                  << " gamma_max=" << gammaMaximum << " gamma_mean=" << gammaMean
                  << " levels=" << mainLevels << ',' << noBloomLevels << ',' << noFxaaLevels
                  << '\n';

        std::vector<std::byte> oddPixels;
        std::vector<std::byte> smallPixels;
        std::size_t oddLevels = 0;
        std::size_t smallLevels = 0;
        if (!processPost({7U, 5U}, true, true, standardComposite, oddPixels, oddLevels) ||
            !processPost({1U, 1U}, true, true, standardComposite, smallPixels, smallLevels)) {
            return false;
        }
        checks.smallTargets = oddPixels.size() == 7U * 5U * 4U &&
                              smallPixels.size() == 4U && oddLevels == 1U &&
                              smallLevels == 1U &&
                              std::to_integer<unsigned>(smallPixels[3]) == 255U;
        std::cout << "Vulkan post target sizes odd_levels=" << oddLevels
                  << " small_levels=" << smallLevels
                  << " status=" << (checks.smallTargets ? "pass" : "fail") << '\n';
        if (!paths.outputDirectory.empty()) {
            checks.post = writeSizedPpm(std::filesystem::path(paths.outputDirectory) /
                                            "vk_hdr.ppm",
                                        {hdrWidth, hdrHeight}, postPixels) &&
                          checks.post;
        }

        const bool allChecks = checks.direct && checks.hdr && checks.lights &&
                               checks.emissive && checks.shadows && checks.skybox &&
                               checks.irradiance && checks.environment && checks.fallback &&
                               checks.post && checks.bloom && checks.toneMapping &&
                               checks.colorControls && checks.gamma && checks.fxaa &&
                               checks.smallTargets && checks.water;
        std::cout << "Vulkan HDR paths direct=" << checks.direct << " hdr=" << checks.hdr
                  << " lights=" << checks.lights << " emissive=" << checks.emissive
                  << " shadows=" << checks.shadows << " skybox=" << checks.skybox
                  << " irradiance=" << checks.irradiance
                  << " environment=" << checks.environment
                  << " fallback=" << checks.fallback << " post=" << checks.post
                  << " bloom=" << checks.bloom << " tonemap=" << checks.toneMapping
                  << " color_controls=" << checks.colorControls
                  << " gamma=" << checks.gamma << " fxaa=" << checks.fxaa
                  << " small_targets=" << checks.smallTargets
                  << " water=" << checks.water
                  << '\n';
        return allChecks;
    }();

    std::cout << "Vulkan HDR allocator blocks=" << allocator->stats().blockCount
              << " live=" << allocator->stats().allocationCount << '\n';
    return passed && allocator->stats().allocationCount == 0U;
}

} // namespace

bool runMaterialPass(const rb::vulkan::Device& device, const MaterialPassPaths& paths) {
    const bool passed = runChecks(device, paths) && runHdrChecks(device, paths);
    std::cout << "Vulkan materials summary status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
