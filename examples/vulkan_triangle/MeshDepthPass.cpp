#include "examples/vulkan_triangle/MeshDepthPass.h"

#include "examples/vulkan_triangle/ObjectsPass.h"
#include "examples/vulkan_triangle/PassCommands.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Barriers.h"
#include "rabbet/render/vulkan/Buffer.h"
#include "rabbet/render/vulkan/Descriptors.h"
#include "rabbet/render/vulkan/Image.h"
#include "rabbet/render/vulkan/OffscreenTarget.h"
#include "rabbet/render/vulkan/Pipeline.h"
#include "rabbet/render/vulkan/Readback.h"
#include "rabbet/render/vulkan/RetireQueue.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace {

// The fixed scene mirrors the capture driver's mesh and depth feature capture, three
// primitives with the engine's exact tessellation, the same camera, the same clear.
// The GL capture of that scene is what the baseline comparison consumes.
constexpr std::uint32_t passWidth = 800;
constexpr std::uint32_t passHeight = 600;
constexpr std::array<float, 4> clearColor{0.30F, 0.37F, 0.47F, 1.0F};

// GL renders the scene depth into a 32 bit float attachment since the D32F adapt, and
// the Vulkan projection seam reproduces the identical viewport depth mapping, so the
// two backends disagree only by rounding inside the two matrix products and by edge
// pixels whose coverage flips between rasterizers. The recorded tolerance allows one
// part in a hundred thousand per pixel with an outlier budget for those edges.
constexpr double depthTolerance = 1.0e-5;
constexpr double outlierBudget = 0.005;

struct DrawRange {
    glm::mat4 model{1.0F};
    glm::vec3 color{1.0F};
    std::uint32_t firstIndex{0};
    std::uint32_t indexCount{0};
    std::int32_t vertexOffset{0};
    glm::vec3 samplePoint{0.0F};
    const char* name{""};
};

struct PushBlock {
    std::array<float, 16> model;
    std::array<float, 3> color;
};
static_assert(sizeof(PushBlock) == 76);

struct PickPushBlock {
    std::array<float, 16> model;
    std::int32_t entityId{0};
};
static_assert(sizeof(PickPushBlock) == 68);

struct DepthPushBlock {
    std::array<float, 16> model;
};
static_assert(sizeof(DepthPushBlock) == 64);

PushBlock makePush(const glm::mat4& model, const glm::vec3& color) {
    PushBlock push{};
    std::memcpy(push.model.data(), &model, sizeof(push.model));
    push.color = {color.x, color.y, color.z};
    return push;
}

PickPushBlock makePickPush(const glm::mat4& model, std::int32_t entityId) {
    PickPushBlock push{};
    std::memcpy(push.model.data(), &model, sizeof(push.model));
    push.entityId = entityId;
    return push;
}

DepthPushBlock makeDepthPush(const glm::mat4& model) {
    DepthPushBlock push{};
    std::memcpy(push.model.data(), &model, sizeof(push.model));
    return push;
}

std::array<long, 2> projectToPixel(const glm::mat4& viewProjection, const glm::vec3& world) {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    // Rows read back top down; the negative viewport height maps ndc y of one to row zero.
    // The clamp keeps a sample point drifting onto the frame edge inside the buffer.
    return {std::clamp(std::lround((ndc.x * 0.5F + 0.5F) * static_cast<float>(passWidth)), 0L,
                       static_cast<long>(passWidth) - 1L),
            std::clamp(std::lround((1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(passHeight)),
                       0L, static_cast<long>(passHeight) - 1L)};
}

float projectToDepth(const glm::mat4& viewProjection, const glm::vec3& world) {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
    return clip.z / clip.w;
}

std::array<unsigned, 4> pixelAt(const std::vector<std::byte>& pixels, long x, long y) {
    const std::size_t base =
        (static_cast<std::size_t>(y) * passWidth + static_cast<std::size_t>(x)) * 4U;
    return {std::to_integer<unsigned>(pixels[base]), std::to_integer<unsigned>(pixels[base + 1U]),
            std::to_integer<unsigned>(pixels[base + 2U]),
            std::to_integer<unsigned>(pixels[base + 3U])};
}

std::array<float, 4> halfPixelAt(const std::vector<std::byte>& pixels, long x, long y) {
    const std::size_t base =
        (static_cast<std::size_t>(y) * passWidth + static_cast<std::size_t>(x)) * 8U;
    std::array<float, 4> result{};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
        std::uint16_t bits = 0;
        std::memcpy(&bits, pixels.data() + base + channel * sizeof(bits), sizeof(bits));
        result[channel] = glm::unpackHalf1x16(bits);
    }
    return result;
}

float depthAt(const std::vector<std::byte>& texels, long x, long y) {
    const std::size_t base =
        (static_cast<std::size_t>(y) * passWidth + static_cast<std::size_t>(x)) * 4U;
    float value = 0.0F;
    std::memcpy(&value, texels.data() + base, sizeof(value));
    return value;
}

bool checkFlatColor(const char* name, const std::vector<std::byte>& pixels, long x, long y,
                    const glm::vec3& color) {
    const std::array<unsigned, 4> actual = pixelAt(pixels, x, y);
    bool passed = actual[3] == 255U;
    const std::array<float, 3> channels{color.x, color.y, color.z};
    std::array<long, 3> expected{};
    for (std::size_t channel = 0; channel < 3U; ++channel) {
        expected[channel] = std::lround(channels[channel] * 255.0F);
        const long delta = static_cast<long>(actual[channel]) - expected[channel];
        passed = passed && delta >= -1 && delta <= 1;
    }
    std::cout << "Vulkan mesh_depth check " << name << " pixel=" << x << ',' << y
              << " expected=" << expected[0] << ',' << expected[1] << ',' << expected[2]
              << " actual=" << actual[0] << ',' << actual[1] << ',' << actual[2]
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool checkShadowDepth(const char* name, const std::vector<std::byte>& texels,
                      const glm::mat4& lightSpace, const glm::vec3& world) {
    const std::array<long, 2> pixel = projectToPixel(lightSpace, world);
    const float expected = projectToDepth(lightSpace, world);
    const float actual = depthAt(texels, pixel[0], pixel[1]);
    const bool passed = std::isfinite(actual) && actual > 0.0F && actual < 1.0F &&
                        std::fabs(actual - expected) <= 5.0e-3F;
    std::cout << "Vulkan mesh_depth check shadow_" << name << " pixel=" << pixel[0] << ','
              << pixel[1] << " expected=" << expected << " actual=" << actual
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool checkHalfColor(const char* name, const std::vector<std::byte>& pixels, long x, long y,
                    const glm::vec3& color) {
    const std::array<float, 4> actual = halfPixelAt(pixels, x, y);
    const std::array<float, 4> expected{color.x, color.y, color.z, 1.0F};
    bool passed = true;
    for (std::size_t channel = 0; channel < actual.size(); ++channel) {
        passed = passed && std::fabs(actual[channel] - expected[channel]) <= 1.0e-3F;
    }
    std::cout << "Vulkan mesh_depth check " << name << " pixel=" << x << ',' << y
              << " expected=" << expected[0] << ',' << expected[1] << ',' << expected[2] << ','
              << expected[3] << " actual=" << actual[0] << ',' << actual[1] << ',' << actual[2]
              << ',' << actual[3]
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool checkDepthCoverage(const char* name, const std::vector<std::byte>& texels) {
    float minimum = 1.0F;
    float maximum = 0.0F;
    std::size_t written = 0;
    bool finiteAndBounded = true;
    for (std::size_t offset = 0; offset < texels.size(); offset += sizeof(float)) {
        float value = 0.0F;
        std::memcpy(&value, texels.data() + offset, sizeof(value));
        finiteAndBounded = finiteAndBounded && std::isfinite(value) && value >= 0.0F &&
                           value <= 1.0F;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        if (value < 1.0F) {
            ++written;
        }
    }
    const bool passed = finiteAndBounded && written > 100U && minimum < 1.0F && maximum == 1.0F;
    std::cout << "Vulkan mesh_depth check " << name << " written=" << written
              << " min=" << minimum << " max=" << maximum
              << " status=" << (passed ? "pass" : "fail") << '\n';
    return passed;
}

bool compareBaselineDepth(const std::string& path, const std::vector<std::byte>& texels) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Vulkan mesh_depth cannot open baseline " << path << '\n';
        return false;
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    const std::size_t expectedSize =
        static_cast<std::size_t>(passWidth) * passHeight * sizeof(float);
    if (size != expectedSize) {
        std::cerr << "Vulkan mesh_depth baseline has " << size << " bytes, expected "
                  << expectedSize << '\n';
        return false;
    }
    std::vector<float> baseline(static_cast<std::size_t>(passWidth) * passHeight);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(baseline.data()),
                   static_cast<std::streamsize>(expectedSize))) {
        std::cerr << "Vulkan mesh_depth baseline read failed\n";
        return false;
    }

    double maxDelta = 0.0;
    double deltaSum = 0.0;
    std::size_t outliers = 0;
    for (std::uint32_t y = 0; y < passHeight; ++y) {
        // The GL capture stores rows bottom to top; the readback rows run top to bottom.
        const std::uint32_t baselineRow = passHeight - 1U - y;
        for (std::uint32_t x = 0; x < passWidth; ++x) {
            const float ours = depthAt(texels, x, y);
            const float theirs = baseline[static_cast<std::size_t>(baselineRow) * passWidth + x];
            const double delta = std::fabs(static_cast<double>(ours) - static_cast<double>(theirs));
            maxDelta = std::max(maxDelta, delta);
            deltaSum += delta;
            if (delta > depthTolerance) {
                ++outliers;
            }
        }
    }
    const auto pixels = static_cast<double>(passWidth) * passHeight;
    const double outlierFraction = static_cast<double>(outliers) / pixels;
    const bool passed = outlierFraction <= outlierBudget;
    std::cout << "Vulkan mesh_depth baseline pixels=" << static_cast<std::size_t>(pixels)
              << " max_delta=" << maxDelta << " mean_delta=" << deltaSum / pixels
              << " over_tolerance=" << outliers << " status=" << (passed ? "pass" : "fail")
              << '\n';
    return passed;
}

bool writeOutputs(const std::string& directory, const std::vector<std::byte>& colorPixels,
                  const std::vector<std::byte>& depthTexels) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        std::cerr << "Vulkan mesh_depth cannot create " << directory << '\n';
        return false;
    }

    std::ofstream ppm(std::filesystem::path(directory) / "vk_mesh_depth.ppm", std::ios::binary);
    ppm << "P6\n" << passWidth << ' ' << passHeight << "\n255\n";
    for (std::size_t i = 0; i < colorPixels.size(); i += 4U) {
        ppm.put(static_cast<char>(std::to_integer<unsigned>(colorPixels[i])));
        ppm.put(static_cast<char>(std::to_integer<unsigned>(colorPixels[i + 1U])));
        ppm.put(static_cast<char>(std::to_integer<unsigned>(colorPixels[i + 2U])));
    }
    if (!ppm) {
        std::cerr << "Vulkan mesh_depth ppm write failed\n";
        return false;
    }

    // Rows flipped to bottom up so the file layout matches the GL capture's raw depth.
    std::ofstream raw(std::filesystem::path(directory) / "vk_mesh_depth_depth.raw",
                      std::ios::binary);
    const std::size_t rowBytes = static_cast<std::size_t>(passWidth) * sizeof(float);
    for (std::uint32_t y = 0; y < passHeight; ++y) {
        const std::uint32_t source = passHeight - 1U - y;
        raw.write(reinterpret_cast<const char*>(depthTexels.data() +
                                                static_cast<std::size_t>(source) * rowBytes),
                  static_cast<std::streamsize>(rowBytes));
    }
    if (!raw) {
        std::cerr << "Vulkan mesh_depth raw write failed\n";
        return false;
    }
    std::cout << "Vulkan mesh_depth outputs written to " << directory << '\n';
    return true;
}

void transitionTarget(VkCommandBuffer commandBuffer,
                      const rb::vulkan::OffscreenTarget& target) {
    std::array<rb::vulkan::ImageBarrier, 2> barriers{};
    std::size_t count = 0;
    if (const rb::vulkan::Image* color = target.color()) {
        auto& barrier = barriers[count++];
        barrier.image = color->handle();
        barrier.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (const rb::vulkan::Image* depth = target.depth()) {
        auto& barrier = barriers[count++];
        barrier.image = depth->handle();
        barrier.dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        barrier.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    rb::vulkan::cmdBarriers(
        commandBuffer,
        std::span<const rb::vulkan::ImageBarrier>(barriers.data(), count), {});
}

void transitionForSampling(VkCommandBuffer commandBuffer, const rb::vulkan::Image& image,
                           VkImageLayout oldLayout, VkPipelineStageFlags2 srcStage,
                           VkAccessFlags2 srcAccess) {
    rb::vulkan::ImageBarrier barrier{};
    barrier.image = image.handle();
    barrier.srcStage = srcStage;
    barrier.srcAccess = srcAccess;
    barrier.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = image.aspect() == VK_IMAGE_ASPECT_DEPTH_BIT
                            ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.aspect = image.aspect();
    rb::vulkan::cmdImageBarrier(commandBuffer, barrier);
}

void bindDrawState(VkCommandBuffer commandBuffer, const rb::vulkan::Pipeline& pipeline,
                   VkDescriptorSet frameSet, const rb::vulkan::Buffer& vertexBuffer,
                   const rb::vulkan::Buffer& indexBuffer, VkExtent2D extent) {
    const VkViewport viewport{0.0F, static_cast<float>(extent.height),
                              static_cast<float>(extent.width),
                              -static_cast<float>(extent.height), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                            1, &frameSet, 0, nullptr);
    const VkBuffer vertexHandle = vertexBuffer.handle();
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, &vertexOffset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
}

void recordFlatTarget(VkCommandBuffer commandBuffer, const rb::vulkan::OffscreenTarget& target,
                      const rb::vulkan::Pipeline& pipeline, VkDescriptorSet frameSet,
                      const rb::vulkan::Buffer& vertexBuffer,
                      const rb::vulkan::Buffer& indexBuffer,
                      const std::array<DrawRange, 3>& draws) {
    transitionTarget(commandBuffer, target);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = target.color()->view();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {
        {clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = target.depth()->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = target.extent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    bindDrawState(commandBuffer, pipeline, frameSet, vertexBuffer, indexBuffer, target.extent());
    for (const DrawRange& draw : draws) {
        const PushBlock push = makePush(draw.model, draw.color);
        vkCmdPushConstants(commandBuffer, pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
    }
    vkCmdEndRendering(commandBuffer);
}

void recordPickTarget(VkCommandBuffer commandBuffer, const rb::vulkan::OffscreenTarget& target,
                      const rb::vulkan::Pipeline& pipeline, VkDescriptorSet frameSet,
                      const rb::vulkan::Buffer& vertexBuffer,
                      const rb::vulkan::Buffer& indexBuffer,
                      const std::array<DrawRange, 3>& draws) {
    transitionTarget(commandBuffer, target);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = target.color()->view();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color.int32[0] = rb::vulkan::OffscreenTarget::noPickId;
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = target.depth()->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = target.extent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    bindDrawState(commandBuffer, pipeline, frameSet, vertexBuffer, indexBuffer, target.extent());
    for (std::size_t index = 0; index < draws.size(); ++index) {
        const DrawRange& draw = draws[index];
        const auto entityId = static_cast<std::int32_t>((index + 1U) * 11U);
        const PickPushBlock push = makePickPush(draw.model, entityId);
        vkCmdPushConstants(commandBuffer, pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
    }
    vkCmdEndRendering(commandBuffer);
}

void recordShadowTarget(VkCommandBuffer commandBuffer, const rb::vulkan::OffscreenTarget& target,
                        const rb::vulkan::Pipeline& pipeline, VkDescriptorSet frameSet,
                        const rb::vulkan::Buffer& vertexBuffer,
                        const rb::vulkan::Buffer& indexBuffer,
                        const std::array<DrawRange, 3>& draws) {
    transitionTarget(commandBuffer, target);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = target.depth()->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = target.extent();
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    bindDrawState(commandBuffer, pipeline, frameSet, vertexBuffer, indexBuffer, target.extent());
    for (const DrawRange& draw : draws) {
        const DepthPushBlock push = makeDepthPush(draw.model);
        vkCmdPushConstants(commandBuffer, pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
    }
    vkCmdEndRendering(commandBuffer);
}

bool runChecks(const rb::vulkan::Device& device, const MeshDepthPassPaths& paths) {
    auto allocator = rb::vulkan::Allocator::create(device);
    if (allocator == nullptr) {
        return false;
    }

    rb::vulkan::OffscreenTargetDescription invalidDescription{};
    invalidDescription.extent = {passWidth, passHeight};
    const bool invalidTargetRejected =
        rb::vulkan::OffscreenTarget::create(device, *allocator, invalidDescription) == nullptr;
    std::cout << "Vulkan mesh_depth check invalid_target status="
              << (invalidTargetRejected ? "pass" : "fail") << '\n';
    if (!invalidTargetRejected) {
        return false;
    }

    const auto vertexCode = loadSpirvFile(paths.vertexSpv);
    const auto fragmentCode = loadSpirvFile(paths.fragmentSpv);
    const auto depthVertexCode = loadSpirvFile(paths.depthVertexSpv);
    const auto depthFragmentCode = loadSpirvFile(paths.depthFragmentSpv);
    const auto pickVertexCode = loadSpirvFile(paths.pickVertexSpv);
    const auto pickFragmentCode = loadSpirvFile(paths.pickFragmentSpv);
    if (vertexCode.empty() || fragmentCode.empty() || depthVertexCode.empty() ||
        depthFragmentCode.empty() || pickVertexCode.empty() || pickFragmentCode.empty()) {
        return false;
    }

    // The camera of the capture scene. The clip fix moves GL's minus one to one depth
    // onto Vulkan's zero to one range at the projection seam and the negative viewport
    // height below flips y, so the shader body stays convention free.
    const glm::mat4 view = glm::lookAt(glm::vec3(3.4F, 2.6F, 4.6F), glm::vec3(0.0F, 0.4F, 0.0F),
                                       glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 projection =
        glm::perspective(glm::radians(50.0F),
                         static_cast<float>(passWidth) / static_cast<float>(passHeight), 0.1F,
                         200.0F);
    glm::mat4 clipFix(1.0F);
    clipFix[2][2] = 0.5F;
    clipFix[3][2] = 0.5F;
    const glm::mat4 viewProjection = clipFix * projection * view;
    const glm::vec3 lightPosition(4.0F, 8.0F, 4.0F);
    const glm::mat4 lightView =
        glm::lookAt(lightPosition, glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 lightProjection = glm::ortho(-7.0F, 7.0F, -7.0F, 7.0F, 1.0F, 30.0F);
    const glm::mat4 lightSpace = clipFix * lightProjection * lightView;

    const rb::MeshData quad = rb::geometry::quad();
    const rb::MeshData cube = rb::geometry::cube();
    const rb::MeshData sphere = rb::geometry::sphere();

    std::vector<rb::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<DrawRange, 3> draws{};
    const std::array<const rb::MeshData*, 3> meshes{&quad, &cube, &sphere};
    const std::array<glm::mat4, 3> models{
        glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, -0.5F, 0.0F)) *
            glm::scale(glm::mat4(1.0F), glm::vec3(12.0F, 1.0F, 12.0F)),
        glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.5F, 0.0F)),
        glm::translate(glm::mat4(1.0F), glm::vec3(1.1F, 0.75F, -1.2F))};
    const std::array<glm::vec3, 3> colors{glm::vec3(0.55F, 0.57F, 0.60F),
                                          glm::vec3(0.82F, 0.34F, 0.28F),
                                          glm::vec3(0.30F, 0.52F, 0.80F)};
    // Interior points of each primitive, projected to sample pixels the flat color
    // must cover. One quad point left of the cube, then the two body centres.
    const std::array<glm::vec3, 3> samples{glm::vec3(-3.0F, -0.4F, 0.0F),
                                           glm::vec3(0.0F, 0.5F, 0.0F),
                                           glm::vec3(1.1F, 0.75F, -1.2F)};
    const std::array<const char*, 3> names{"quad", "cube", "sphere"};
    const glm::vec3 sphereCenter(1.1F, 0.75F, -1.2F);
    const std::array<glm::vec3, 3> shadowSamples{
        samples[0], glm::vec3(0.0F, 1.0F, 0.0F),
        sphereCenter + 0.5F * glm::normalize(lightPosition - sphereCenter)};
    for (std::size_t i = 0; i < 3U; ++i) {
        DrawRange& draw = draws[i];
        draw.model = models[i];
        draw.color = colors[i];
        draw.samplePoint = samples[i];
        draw.name = names[i];
        draw.firstIndex = static_cast<std::uint32_t>(indices.size());
        draw.indexCount = static_cast<std::uint32_t>(meshes[i]->indices.size());
        draw.vertexOffset = static_cast<std::int32_t>(vertices.size());
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
    auto uniformBuffer =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(viewProjection),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto lightUniformBuffer =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(lightSpace),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto vertexStaging =
        rb::vulkan::Buffer::create(device, *allocator, vertexBytes,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto indexStaging =
        rb::vulkan::Buffer::create(device, *allocator, indexBytes,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    if (vertexBuffer == nullptr || indexBuffer == nullptr || uniformBuffer == nullptr ||
        lightUniformBuffer == nullptr || vertexStaging == nullptr || indexStaging == nullptr) {
        return false;
    }

    rb::vulkan::OffscreenTargetDescription rgba8Description{};
    rgba8Description.extent = {passWidth, passHeight};
    rgba8Description.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    rgba8Description.depth = true;
    rgba8Description.sampledColor = true;
    auto rgba8Target = rb::vulkan::OffscreenTarget::create(device, *allocator, rgba8Description);

    rb::vulkan::OffscreenTargetDescription rgba16Description = rgba8Description;
    rgba16Description.colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    auto rgba16Target = rb::vulkan::OffscreenTarget::create(device, *allocator, rgba16Description);

    rb::vulkan::OffscreenTargetDescription pickDescription = rgba8Description;
    pickDescription.colorFormat = VK_FORMAT_R32_SINT;
    pickDescription.sampledColor = false;
    auto pickTarget = rb::vulkan::OffscreenTarget::create(device, *allocator, pickDescription);

    rb::vulkan::OffscreenTargetDescription shadowDescription{};
    shadowDescription.extent = {passWidth, passHeight};
    shadowDescription.depth = true;
    shadowDescription.sampledDepth = true;
    auto shadowTarget = rb::vulkan::OffscreenTarget::create(device, *allocator, shadowDescription);
    if (rgba8Target == nullptr || rgba16Target == nullptr || pickTarget == nullptr ||
        shadowTarget == nullptr) {
        return false;
    }

    std::memcpy(vertexStaging->mapped(), vertices.data(), vertexBytes);
    std::memcpy(indexStaging->mapped(), indices.data(), indexBytes);
    std::memcpy(uniformBuffer->mapped(), &viewProjection, sizeof(viewProjection));
    std::memcpy(lightUniformBuffer->mapped(), &lightSpace, sizeof(lightSpace));
    if (!vertexStaging->flush(0, vertexBytes) || !indexStaging->flush(0, indexBytes) ||
        !uniformBuffer->flush(0, sizeof(viewProjection)) ||
        !lightUniformBuffer->flush(0, sizeof(lightSpace))) {
        std::cerr << "Vulkan mesh_depth staging flush failed\n";
        return false;
    }

    PassCommands commands;
    if (!commands.create(device.handle(), device.graphicsQueueFamily(), "mesh_depth") ||
        !commands.begin()) {
        return false;
    }
    VkBufferCopy vertexRegion{0, 0, vertexBytes};
    vkCmdCopyBuffer(commands.buffer, vertexStaging->handle(), vertexBuffer->handle(), 1,
                    &vertexRegion);
    VkBufferCopy indexRegion{0, 0, indexBytes};
    vkCmdCopyBuffer(commands.buffer, indexStaging->handle(), indexBuffer->handle(), 1,
                    &indexRegion);
    rb::vulkan::BufferBarrier vertexReady{};
    vertexReady.buffer = vertexBuffer->handle();
    vertexReady.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    vertexReady.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vertexReady.dstStage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    vertexReady.dstAccess = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    rb::vulkan::BufferBarrier indexReady{};
    indexReady.buffer = indexBuffer->handle();
    indexReady.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    indexReady.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    indexReady.dstStage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    indexReady.dstAccess = VK_ACCESS_2_INDEX_READ_BIT;
    const std::array<rb::vulkan::BufferBarrier, 2> uploadBarriers{vertexReady, indexReady};
    rb::vulkan::cmdBarriers(commands.buffer, {}, uploadBarriers);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    rb::vulkan::RetireQueue retireQueue;
    retireQueue.beginFrame(1);
    retireQueue.retire(std::move(vertexStaging));
    retireQueue.retire(std::move(indexStaging));
    retireQueue.collect(1);

    const std::array<VkDescriptorSetLayoutBinding, 1> frameBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
    auto frameLayout = rb::vulkan::DescriptorSetLayout::create(device, frameBindings);
    if (frameLayout == nullptr) {
        return false;
    }
    const std::array<VkDescriptorPoolSize, 1> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}};
    auto descriptorPool = rb::vulkan::DescriptorPool::create(device, 2, poolSizes);
    if (descriptorPool == nullptr) {
        return false;
    }
    const VkDescriptorSet frameSet = descriptorPool->allocate(frameLayout->handle());
    const VkDescriptorSet lightFrameSet = descriptorPool->allocate(frameLayout->handle());
    if (frameSet == VK_NULL_HANDLE || lightFrameSet == VK_NULL_HANDLE) {
        return false;
    }
    VkDescriptorBufferInfo uniformInfo{uniformBuffer->handle(), 0, sizeof(viewProjection)};
    VkDescriptorBufferInfo lightUniformInfo{lightUniformBuffer->handle(), 0, sizeof(lightSpace)};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = frameSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uniformInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = lightFrameSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &lightUniformInfo;
    vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    const std::array<rb::vulkan::VertexAttribute, 1> attributes{
        rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
    const std::array<VkFormat, 1> rgba8Formats{VK_FORMAT_R8G8B8A8_UNORM};
    const std::array<VkFormat, 1> rgba16Formats{VK_FORMAT_R16G16B16A16_SFLOAT};
    const std::array<VkFormat, 1> pickFormats{VK_FORMAT_R32_SINT};
    const std::array<VkDescriptorSetLayout, 1> setLayouts{frameLayout->handle()};

    rb::vulkan::PipelineDescription flatDescription{};
    flatDescription.vertexCode = vertexCode;
    flatDescription.fragmentCode = fragmentCode;
    flatDescription.vertexStride = sizeof(rb::Vertex);
    flatDescription.vertexAttributes = attributes;
    flatDescription.depthTest = true;
    flatDescription.depthWrite = true;
    flatDescription.colorFormats = rgba8Formats;
    flatDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
    flatDescription.setLayouts = setLayouts;
    flatDescription.pushConstantBytes = sizeof(PushBlock);
    auto rgba8Pipeline =
        rb::vulkan::Pipeline::create(device, flatDescription, VK_NULL_HANDLE);
    flatDescription.colorFormats = rgba16Formats;
    auto rgba16Pipeline =
        rb::vulkan::Pipeline::create(device, flatDescription, VK_NULL_HANDLE);

    rb::vulkan::PipelineDescription pickPipelineDescription{};
    pickPipelineDescription.vertexCode = pickVertexCode;
    pickPipelineDescription.fragmentCode = pickFragmentCode;
    pickPipelineDescription.vertexStride = sizeof(rb::Vertex);
    pickPipelineDescription.vertexAttributes = attributes;
    pickPipelineDescription.depthTest = true;
    pickPipelineDescription.depthWrite = true;
    pickPipelineDescription.colorFormats = pickFormats;
    pickPipelineDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
    pickPipelineDescription.setLayouts = setLayouts;
    pickPipelineDescription.pushConstantBytes = sizeof(PickPushBlock);
    auto pickPipeline =
        rb::vulkan::Pipeline::create(device, pickPipelineDescription, VK_NULL_HANDLE);

    rb::vulkan::PipelineDescription shadowPipelineDescription{};
    shadowPipelineDescription.vertexCode = depthVertexCode;
    shadowPipelineDescription.fragmentCode = depthFragmentCode;
    shadowPipelineDescription.vertexStride = sizeof(rb::Vertex);
    shadowPipelineDescription.vertexAttributes = attributes;
    shadowPipelineDescription.depthTest = true;
    shadowPipelineDescription.depthWrite = true;
    shadowPipelineDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
    shadowPipelineDescription.setLayouts = setLayouts;
    shadowPipelineDescription.pushConstantBytes = sizeof(DepthPushBlock);
    shadowPipelineDescription.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    auto shadowPipeline =
        rb::vulkan::Pipeline::create(device, shadowPipelineDescription, VK_NULL_HANDLE);

    if (rgba8Pipeline == nullptr || rgba16Pipeline == nullptr || pickPipeline == nullptr ||
        shadowPipeline == nullptr) {
        return false;
    }

    if (!commands.begin()) {
        return false;
    }
    recordFlatTarget(commands.buffer, *rgba8Target, *rgba8Pipeline, frameSet, *vertexBuffer,
                     *indexBuffer, draws);
    recordFlatTarget(commands.buffer, *rgba16Target, *rgba16Pipeline, frameSet, *vertexBuffer,
                     *indexBuffer, draws);
    recordPickTarget(commands.buffer, *pickTarget, *pickPipeline, frameSet, *vertexBuffer,
                     *indexBuffer, draws);
    recordShadowTarget(commands.buffer, *shadowTarget, *shadowPipeline, lightFrameSet,
                       *vertexBuffer, *indexBuffer, draws);
    transitionForSampling(commands.buffer, *rgba8Target->color(),
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transitionForSampling(commands.buffer, *rgba16Target->color(),
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transitionForSampling(commands.buffer, *shadowTarget->depth(),
                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    bool checksPassed = false;
    {
        auto readback = rb::vulkan::Readback::create(device, *allocator);
        if (readback == nullptr) {
            return false;
        }
        const std::size_t pixelCount = static_cast<std::size_t>(passWidth) * passHeight;
        std::vector<std::byte> colorPixels(pixelCount * 4U);
        std::vector<std::byte> halfPixels(pixelCount * 8U);
        std::vector<std::byte> depthTexels(pixelCount * 4U);
        std::vector<std::byte> shadowTexels(pixelCount * 4U);
        constexpr VkPipelineStageFlags2 colorStage =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        constexpr VkAccessFlags2 colorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        constexpr VkPipelineStageFlags2 depthStage =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        constexpr VkAccessFlags2 depthAccess =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        constexpr VkPipelineStageFlags2 sampledStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        constexpr VkAccessFlags2 sampledAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        if (!rgba8Target->readColor(*readback, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    sampledStage, sampledAccess, colorPixels)) {
            return false;
        }
        if (!rgba8Target->readDepth(*readback, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                    depthStage, depthAccess, depthTexels)) {
            return false;
        }
        if (!rgba16Target->readColor(*readback, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     sampledStage, sampledAccess, halfPixels)) {
            return false;
        }
        if (!shadowTarget->readDepth(*readback, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                                     sampledStage, sampledAccess, shadowTexels)) {
            return false;
        }

        checksPassed = true;
        const std::array<unsigned, 4> corner = pixelAt(colorPixels, 2, 2);
        const bool cornerPassed = checkFlatColor("background", colorPixels, 2, 2,
                                                 glm::vec3(clearColor[0], clearColor[1],
                                                           clearColor[2]));
        checksPassed = checksPassed && cornerPassed && corner[3] == 255U;
        checksPassed =
            checkHalfColor("rgba16_background", halfPixels, 2, 2,
                           glm::vec3(clearColor[0], clearColor[1], clearColor[2])) &&
            checksPassed;
        const float cornerDepth = depthAt(depthTexels, 2, 2);
        const bool cornerDepthPassed = cornerDepth == 1.0F;
        std::cout << "Vulkan mesh_depth check background_depth expected=1 actual=" << cornerDepth
                  << " status=" << (cornerDepthPassed ? "pass" : "fail") << '\n';
        checksPassed = checksPassed && cornerDepthPassed;

        for (const DrawRange& draw : draws) {
            const std::array<long, 2> pixel = projectToPixel(viewProjection, draw.samplePoint);
            checksPassed =
                checkFlatColor(draw.name, colorPixels, pixel[0], pixel[1], draw.color) &&
                checksPassed;
            const std::string halfName = std::string("rgba16_") + draw.name;
            checksPassed =
                checkHalfColor(halfName.c_str(), halfPixels, pixel[0], pixel[1], draw.color) &&
                checksPassed;
            const float depth = depthAt(depthTexels, pixel[0], pixel[1]);
            const bool depthPassed = depth > 0.0F && depth < 1.0F;
            std::cout << "Vulkan mesh_depth check " << draw.name << "_depth actual=" << depth
                      << " status=" << (depthPassed ? "pass" : "fail") << '\n';
            checksPassed = checksPassed && depthPassed;
        }

        const auto checkPick = [&](const char* name, std::int32_t x, std::int32_t y,
                                   std::int32_t expected) {
            std::int32_t actual = rb::vulkan::OffscreenTarget::noPickId;
            const bool read = pickTarget->readPickId(
                *readback, x, y, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorStage,
                colorAccess, actual);
            const bool passed = read && actual == expected;
            std::cout << "Vulkan mesh_depth check " << name << " expected=" << expected
                      << " actual=" << actual << " status=" << (passed ? "pass" : "fail")
                      << '\n';
            return passed;
        };
        checksPassed = checkPick("pick_background", 2, 2,
                                 rb::vulkan::OffscreenTarget::noPickId) &&
                       checksPassed;
        for (std::size_t index = 0; index < draws.size(); ++index) {
            const DrawRange& draw = draws[index];
            const std::array<long, 2> pixel = projectToPixel(viewProjection, draw.samplePoint);
            const std::string pickName = std::string("pick_") + draw.name;
            const auto expected = static_cast<std::int32_t>((index + 1U) * 11U);
            checksPassed = checkPick(pickName.c_str(), static_cast<std::int32_t>(pixel[0]),
                                    static_cast<std::int32_t>(pixel[1]), expected) &&
                           checksPassed;
        }
        std::int32_t outside = 0;
        const bool pickBoundsPassed =
            pickTarget->readPickId(*readback, -1, 0,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorStage,
                                   colorAccess, outside) &&
            outside == rb::vulkan::OffscreenTarget::noPickId &&
            pickTarget->readPickId(*readback, static_cast<std::int32_t>(passWidth), 0,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorStage,
                                   colorAccess, outside) &&
            outside == rb::vulkan::OffscreenTarget::noPickId;
        std::cout << "Vulkan mesh_depth check pick_bounds status="
                  << (pickBoundsPassed ? "pass" : "fail") << '\n';
        checksPassed = checksPassed && pickBoundsPassed;
        checksPassed = checkDepthCoverage("shadow_depth", shadowTexels) && checksPassed;
        for (std::size_t index = 0; index < draws.size(); ++index) {
            checksPassed = checkShadowDepth(draws[index].name, shadowTexels, lightSpace,
                                            shadowSamples[index]) &&
                           checksPassed;
        }

        if (!paths.baselineDepthRaw.empty()) {
            checksPassed = compareBaselineDepth(paths.baselineDepthRaw, depthTexels) &&
                           checksPassed;
        }
        if (!paths.outputDirectory.empty()) {
            checksPassed = writeOutputs(paths.outputDirectory, colorPixels, depthTexels) &&
                           checksPassed;
        }
    }

    retireQueue.beginFrame(2);
    retireQueue.retire(std::move(vertexBuffer));
    retireQueue.retire(std::move(indexBuffer));
    retireQueue.retire(std::move(uniformBuffer));
    retireQueue.retire(std::move(lightUniformBuffer));
    retireQueue.collect(2);
    rgba8Target.reset();
    rgba16Target.reset();
    pickTarget.reset();
    shadowTarget.reset();

    const rb::vulkan::AllocatorStats& stats = allocator->stats();
    std::cout << "Vulkan mesh_depth allocator blocks=" << stats.blockCount
              << " live=" << stats.allocationCount << '\n';
    if (stats.allocationCount != 0U) {
        return false;
    }
    return checksPassed;
}

}

bool runMeshDepthPass(const rb::vulkan::Device& device, const MeshDepthPassPaths& paths) {
    const bool passed = runChecks(device, paths);
    std::cout << "Vulkan mesh_depth summary status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
