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
#include "rabbet/render/vulkan/Pipeline.h"
#include "rabbet/render/vulkan/Readback.h"
#include "rabbet/render/vulkan/RetireQueue.h"
#include "rabbet/render/vulkan/Sampler.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
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
        !readback->readImage(colorTarget->handle(), colorTarget->format(), colorTarget->extent(),
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

} // namespace

bool runMaterialPass(const rb::vulkan::Device& device, const MaterialPassPaths& paths) {
    const bool passed = runChecks(device, paths);
    std::cout << "Vulkan materials summary status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
