#include "examples/vulkan_triangle/MeshDepthPass.h"

#include "examples/vulkan_triangle/ObjectsPass.h"
#include "examples/vulkan_triangle/PassCommands.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Barriers.h"
#include "rabbet/render/vulkan/Buffer.h"
#include "rabbet/render/vulkan/Descriptors.h"
#include "rabbet/render/vulkan/Image.h"
#include "rabbet/render/vulkan/Pipeline.h"
#include "rabbet/render/vulkan/Readback.h"
#include "rabbet/render/vulkan/RetireQueue.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

PushBlock makePush(const glm::mat4& model, const glm::vec3& color) {
    PushBlock push{};
    std::memcpy(push.model.data(), &model, sizeof(push.model));
    push.color = {color.x, color.y, color.z};
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

std::array<unsigned, 4> pixelAt(const std::vector<std::byte>& pixels, long x, long y) {
    const std::size_t base =
        (static_cast<std::size_t>(y) * passWidth + static_cast<std::size_t>(x)) * 4U;
    return {std::to_integer<unsigned>(pixels[base]), std::to_integer<unsigned>(pixels[base + 1U]),
            std::to_integer<unsigned>(pixels[base + 2U]),
            std::to_integer<unsigned>(pixels[base + 3U])};
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

bool runChecks(const rb::vulkan::Device& device, const MeshDepthPassPaths& paths) {
    auto allocator = rb::vulkan::Allocator::create(device);
    if (allocator == nullptr) {
        return false;
    }

    const auto vertexCode = loadSpirvFile(paths.vertexSpv);
    const auto fragmentCode = loadSpirvFile(paths.fragmentSpv);
    if (vertexCode.empty() || fragmentCode.empty()) {
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
    auto vertexStaging =
        rb::vulkan::Buffer::create(device, *allocator, vertexBytes,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto indexStaging =
        rb::vulkan::Buffer::create(device, *allocator, indexBytes,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    if (vertexBuffer == nullptr || indexBuffer == nullptr || uniformBuffer == nullptr ||
        vertexStaging == nullptr || indexStaging == nullptr) {
        return false;
    }

    rb::vulkan::ImageDescription colorDescription{};
    colorDescription.extent = VkExtent2D{passWidth, passHeight};
    colorDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorDescription.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto colorTarget = rb::vulkan::Image::create(device, *allocator, colorDescription);
    rb::vulkan::ImageDescription depthDescription{};
    depthDescription.extent = VkExtent2D{passWidth, passHeight};
    depthDescription.format = VK_FORMAT_D32_SFLOAT;
    depthDescription.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto depthTarget = rb::vulkan::Image::create(device, *allocator, depthDescription);
    if (colorTarget == nullptr || depthTarget == nullptr) {
        return false;
    }

    std::memcpy(vertexStaging->mapped(), vertices.data(), vertexBytes);
    std::memcpy(indexStaging->mapped(), indices.data(), indexBytes);
    std::memcpy(uniformBuffer->mapped(), &viewProjection, sizeof(viewProjection));
    if (!vertexStaging->flush(0, vertexBytes) || !indexStaging->flush(0, indexBytes) ||
        !uniformBuffer->flush(0, sizeof(viewProjection))) {
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
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    auto descriptorPool = rb::vulkan::DescriptorPool::create(device, 1, poolSizes);
    if (descriptorPool == nullptr) {
        return false;
    }
    const VkDescriptorSet frameSet = descriptorPool->allocate(frameLayout->handle());
    if (frameSet == VK_NULL_HANDLE) {
        return false;
    }
    VkDescriptorBufferInfo uniformInfo{uniformBuffer->handle(), 0, sizeof(viewProjection)};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frameSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &uniformInfo;
    vkUpdateDescriptorSets(device.handle(), 1, &write, 0, nullptr);

    const std::array<rb::vulkan::VertexAttribute, 1> attributes{
        rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
    const std::array<VkFormat, 1> colorFormats{VK_FORMAT_R8G8B8A8_UNORM};
    const std::array<VkDescriptorSetLayout, 1> setLayouts{frameLayout->handle()};
    rb::vulkan::PipelineDescription pipelineDescription{};
    pipelineDescription.vertexCode = vertexCode;
    pipelineDescription.fragmentCode = fragmentCode;
    pipelineDescription.vertexStride = sizeof(rb::Vertex);
    pipelineDescription.vertexAttributes = attributes;
    pipelineDescription.depthTest = true;
    pipelineDescription.depthWrite = true;
    pipelineDescription.colorFormats = colorFormats;
    pipelineDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
    pipelineDescription.setLayouts = setLayouts;
    pipelineDescription.pushConstantBytes = sizeof(PushBlock);
    auto pipeline = rb::vulkan::Pipeline::create(device, pipelineDescription, VK_NULL_HANDLE);
    if (pipeline == nullptr) {
        return false;
    }

    if (!commands.begin()) {
        return false;
    }
    rb::vulkan::ImageBarrier colorToAttachment{};
    colorToAttachment.image = colorTarget->handle();
    colorToAttachment.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    colorToAttachment.dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    colorToAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorToAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rb::vulkan::ImageBarrier depthToAttachment{};
    depthToAttachment.image = depthTarget->handle();
    depthToAttachment.dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthToAttachment.dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthToAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthToAttachment.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    const std::array<rb::vulkan::ImageBarrier, 2> renderBarriers{colorToAttachment,
                                                                 depthToAttachment};
    rb::vulkan::cmdBarriers(commands.buffer, renderBarriers, {});

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorTarget->view();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{clearColor[0], clearColor[1], clearColor[2],
                                         clearColor[3]}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthTarget->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = VkExtent2D{passWidth, passHeight};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commands.buffer, &renderingInfo);

    // The negative height flips y so the engine's GL convention renders upright.
    const VkViewport viewport{0.0F, static_cast<float>(passHeight),
                              static_cast<float>(passWidth), -static_cast<float>(passHeight),
                              0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, {passWidth, passHeight}};
    vkCmdSetViewport(commands.buffer, 0, 1, &viewport);
    vkCmdSetScissor(commands.buffer, 0, 1, &scissor);
    vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
    vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout(),
                            0, 1, &frameSet, 0, nullptr);
    const VkBuffer vertexHandle = vertexBuffer->handle();
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
    vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    for (const DrawRange& draw : draws) {
        const PushBlock push = makePush(draw.model, draw.color);
        vkCmdPushConstants(commands.buffer, pipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(commands.buffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset,
                         0);
    }
    vkCmdEndRendering(commands.buffer);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    bool checksPassed = false;
    {
        auto readback = rb::vulkan::Readback::create(device, *allocator);
        if (readback == nullptr) {
            return false;
        }
        std::vector<std::byte> colorPixels(static_cast<std::size_t>(passWidth) * passHeight * 4U);
        std::vector<std::byte> depthTexels(static_cast<std::size_t>(passWidth) * passHeight * 4U);
        if (!readback->readImage(colorTarget->handle(), colorTarget->format(),
                                 colorTarget->extent(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, colorPixels)) {
            return false;
        }
        if (!readback->readImage(depthTarget->handle(), depthTarget->format(),
                                 depthTarget->extent(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 depthTexels)) {
            return false;
        }

        checksPassed = true;
        const std::array<unsigned, 4> corner = pixelAt(colorPixels, 2, 2);
        const bool cornerPassed = checkFlatColor("background", colorPixels, 2, 2,
                                                 glm::vec3(clearColor[0], clearColor[1],
                                                           clearColor[2]));
        checksPassed = checksPassed && cornerPassed && corner[3] == 255U;
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
            const float depth = depthAt(depthTexels, pixel[0], pixel[1]);
            const bool depthPassed = depth > 0.0F && depth < 1.0F;
            std::cout << "Vulkan mesh_depth check " << draw.name << "_depth actual=" << depth
                      << " status=" << (depthPassed ? "pass" : "fail") << '\n';
            checksPassed = checksPassed && depthPassed;
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
    retireQueue.retire(std::move(colorTarget));
    retireQueue.retire(std::move(depthTarget));
    retireQueue.collect(2);

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
