#include "examples/vulkan_triangle/ObjectsPass.h"

#include "examples/vulkan_triangle/PassCommands.h"
#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Barriers.h"
#include "rabbet/render/vulkan/Buffer.h"
#include "rabbet/render/vulkan/Descriptors.h"
#include "rabbet/render/vulkan/Image.h"
#include "rabbet/render/vulkan/Pipeline.h"
#include "rabbet/render/vulkan/PipelineCache.h"
#include "rabbet/render/vulkan/Readback.h"
#include "rabbet/render/vulkan/RetireQueue.h"
#include "rabbet/render/vulkan/Sampler.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr std::uint32_t passExtent = 256;
constexpr std::uint32_t textureExtent = 4;

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 2> uv;
    std::array<float, 4> color;
};
static_assert(sizeof(Vertex) == 36);

struct ColorCheck {
    const char* name{""};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::array<std::uint8_t, 4> expected{};
};

struct DepthCheck {
    const char* name{""};
    std::uint32_t x{0};
    std::uint32_t y{0};
    float expected{0.0F};
};

const char* cacheStatusName(rb::vulkan::PipelineCacheLoad load) {
    switch (load) {
        case rb::vulkan::PipelineCacheLoad::fresh:
            return "fresh";
        case rb::vulkan::PipelineCacheLoad::loaded:
            return "loaded";
        case rb::vulkan::PipelineCacheLoad::rejected:
            return "rejected";
    }
    return "fresh";
}

bool checkColors(const std::vector<std::byte>& pixels) {
    const std::array<ColorCheck, 4> checks{
        ColorCheck{"corner", 4, 4, {0, 0, 0, 255}},
        ColorCheck{"white_cell", 50, 50, {255, 0, 0, 255}},
        ColorCheck{"blue_cell", 102, 50, {0, 0, 0, 255}},
        ColorCheck{"center", 128, 128, {0, 0, 255, 255}},
    };
    bool allPassed = true;
    for (const ColorCheck& check : checks) {
        const std::size_t base =
            (static_cast<std::size_t>(check.y) * passExtent + check.x) * 4U;
        std::array<unsigned, 4> actual{};
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            actual[channel] = std::to_integer<unsigned>(pixels[base + channel]);
        }
        const bool passed = actual[0] == check.expected[0] && actual[1] == check.expected[1] &&
                            actual[2] == check.expected[2] && actual[3] == check.expected[3];
        allPassed = allPassed && passed;
        std::cout << "Vulkan objects check " << check.name << " expected="
                  << static_cast<unsigned>(check.expected[0]) << ','
                  << static_cast<unsigned>(check.expected[1]) << ','
                  << static_cast<unsigned>(check.expected[2]) << ','
                  << static_cast<unsigned>(check.expected[3]) << " actual=" << actual[0] << ','
                  << actual[1] << ',' << actual[2] << ',' << actual[3]
                  << " status=" << (passed ? "pass" : "fail") << '\n';
    }
    return allPassed;
}

bool checkDepths(const std::vector<std::byte>& texels) {
    const std::array<DepthCheck, 3> checks{
        DepthCheck{"depth_corner", 4, 4, 1.0F},
        DepthCheck{"depth_quad", 50, 50, 0.8F},
        DepthCheck{"depth_center", 128, 128, 0.2F},
    };
    bool allPassed = true;
    for (const DepthCheck& check : checks) {
        const std::size_t base =
            (static_cast<std::size_t>(check.y) * passExtent + check.x) * 4U;
        float actual = 0.0F;
        std::memcpy(&actual, texels.data() + base, sizeof(actual));
        const bool passed = std::fabs(actual - check.expected) <= 0.000001F;
        allPassed = allPassed && passed;
        std::cout << "Vulkan objects check " << check.name << " expected=" << check.expected
                  << " actual=" << actual << " status=" << (passed ? "pass" : "fail") << '\n';
    }
    return allPassed;
}

bool runChecks(const rb::vulkan::Device& device, const ObjectsPassPaths& paths) {
    auto allocator = rb::vulkan::Allocator::create(device);
    if (allocator == nullptr) {
        return false;
    }
    auto pipelineCache = rb::vulkan::PipelineCache::create(device, paths.pipelineCache);
    if (pipelineCache == nullptr) {
        return false;
    }
    std::cout << "Vulkan objects cache_status=" << cacheStatusName(pipelineCache->loadResult())
              << " loaded_bytes=" << pipelineCache->loadedBytes() << '\n';

    const auto vertexCode = loadSpirvFile(paths.vertexSpv);
    const auto fragmentCode = loadSpirvFile(paths.fragmentSpv);
    if (vertexCode.empty() || fragmentCode.empty()) {
        return false;
    }

    const std::array<Vertex, 7> vertices{
        Vertex{{-0.5F, -0.5F, 0.2F}, {0.125F, 0.125F}, {1.0F, 1.0F, 1.0F, 1.0F}},
        Vertex{{0.5F, -0.5F, 0.2F}, {0.125F, 0.125F}, {1.0F, 1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.5F, 0.2F}, {0.125F, 0.125F}, {1.0F, 1.0F, 1.0F, 1.0F}},
        Vertex{{-0.8F, -0.8F, 0.8F}, {0.0F, 0.0F}, {1.0F, 1.0F, 0.0F, 1.0F}},
        Vertex{{0.8F, -0.8F, 0.8F}, {1.0F, 0.0F}, {1.0F, 1.0F, 0.0F, 1.0F}},
        Vertex{{0.8F, 0.8F, 0.8F}, {1.0F, 1.0F}, {1.0F, 1.0F, 0.0F, 1.0F}},
        Vertex{{-0.8F, 0.8F, 0.8F}, {0.0F, 1.0F}, {1.0F, 1.0F, 0.0F, 1.0F}},
    };
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};
    const std::array<float, 4> frameTint{1.0F, 0.0F, 1.0F, 1.0F};
    const std::array<float, 4> trianglePush{0.0F, 0.0F, 1.0F, 1.0F};
    const std::array<float, 4> quadPush{1.0F, 1.0F, 1.0F, 1.0F};

    std::array<std::uint8_t, textureExtent * textureExtent * 4> texels{};
    for (std::uint32_t y = 0; y < textureExtent; ++y) {
        for (std::uint32_t x = 0; x < textureExtent; ++x) {
            const std::size_t base = (static_cast<std::size_t>(y) * textureExtent + x) * 4U;
            const bool white = (x + y) % 2U == 0U;
            texels[base] = white ? std::uint8_t{255} : std::uint8_t{0};
            texels[base + 1U] = white ? std::uint8_t{255} : std::uint8_t{0};
            texels[base + 2U] = std::uint8_t{255};
            texels[base + 3U] = std::uint8_t{255};
        }
    }

    auto vertexBuffer = rb::vulkan::Buffer::create(
        device, *allocator, sizeof(vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        rb::vulkan::MemoryClass::deviceOnly);
    auto indexBuffer = rb::vulkan::Buffer::create(
        device, *allocator, sizeof(indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        rb::vulkan::MemoryClass::deviceOnly);
    auto uniformBuffer =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(frameTint),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto vertexStaging =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(vertices),
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto indexStaging =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(indices),
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    auto texelStaging =
        rb::vulkan::Buffer::create(device, *allocator, sizeof(texels),
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   rb::vulkan::MemoryClass::hostUpload);
    if (vertexBuffer == nullptr || indexBuffer == nullptr || uniformBuffer == nullptr ||
        vertexStaging == nullptr || indexStaging == nullptr || texelStaging == nullptr) {
        return false;
    }

    rb::vulkan::ImageDescription textureDescription{};
    textureDescription.extent = VkExtent2D{textureExtent, textureExtent};
    textureDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    textureDescription.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto texture = rb::vulkan::Image::create(device, *allocator, textureDescription);
    rb::vulkan::ImageDescription colorDescription{};
    colorDescription.extent = VkExtent2D{passExtent, passExtent};
    colorDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorDescription.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto colorTarget = rb::vulkan::Image::create(device, *allocator, colorDescription);
    rb::vulkan::ImageDescription depthDescription{};
    depthDescription.extent = VkExtent2D{passExtent, passExtent};
    depthDescription.format = VK_FORMAT_D32_SFLOAT;
    depthDescription.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto depthTarget = rb::vulkan::Image::create(device, *allocator, depthDescription);
    if (texture == nullptr || colorTarget == nullptr || depthTarget == nullptr) {
        return false;
    }

    std::memcpy(vertexStaging->mapped(), vertices.data(), sizeof(vertices));
    std::memcpy(indexStaging->mapped(), indices.data(), sizeof(indices));
    std::memcpy(texelStaging->mapped(), texels.data(), sizeof(texels));
    std::memcpy(uniformBuffer->mapped(), frameTint.data(), sizeof(frameTint));
    if (!vertexStaging->flush(0, sizeof(vertices)) || !indexStaging->flush(0, sizeof(indices)) ||
        !texelStaging->flush(0, sizeof(texels)) || !uniformBuffer->flush(0, sizeof(frameTint))) {
        std::cerr << "Vulkan objects staging flush failed\n";
        return false;
    }

    PassCommands commands;
    if (!commands.create(device.handle(), device.graphicsQueueFamily(), "objects") ||
        !commands.begin()) {
        return false;
    }
    VkBufferCopy vertexRegion{0, 0, sizeof(vertices)};
    vkCmdCopyBuffer(commands.buffer, vertexStaging->handle(), vertexBuffer->handle(), 1,
                    &vertexRegion);
    VkBufferCopy indexRegion{0, 0, sizeof(indices)};
    vkCmdCopyBuffer(commands.buffer, indexStaging->handle(), indexBuffer->handle(), 1,
                    &indexRegion);

    rb::vulkan::ImageBarrier textureToTransfer{};
    textureToTransfer.image = texture->handle();
    textureToTransfer.dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    textureToTransfer.dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    textureToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    rb::vulkan::cmdImageBarrier(commands.buffer, textureToTransfer);
    VkBufferImageCopy texelRegion{};
    texelRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    texelRegion.imageSubresource.layerCount = 1;
    texelRegion.imageExtent = VkExtent3D{textureExtent, textureExtent, 1U};
    vkCmdCopyBufferToImage(commands.buffer, texelStaging->handle(), texture->handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &texelRegion);

    rb::vulkan::ImageBarrier textureToSampled{};
    textureToSampled.image = texture->handle();
    textureToSampled.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    textureToSampled.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    textureToSampled.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    textureToSampled.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    textureToSampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    textureToSampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    const std::array<rb::vulkan::ImageBarrier, 1> uploadImageBarriers{textureToSampled};
    const std::array<rb::vulkan::BufferBarrier, 2> uploadBufferBarriers{vertexReady, indexReady};
    rb::vulkan::cmdBarriers(commands.buffer, uploadImageBarriers, uploadBufferBarriers);
    if (!commands.submitAndWait(device.graphicsQueue())) {
        return false;
    }

    rb::vulkan::RetireQueue retireQueue;
    retireQueue.beginFrame(1);
    retireQueue.retire(std::move(vertexStaging));
    retireQueue.retire(std::move(indexStaging));
    retireQueue.retire(std::move(texelStaging));
    const std::size_t pendingBefore = retireQueue.pendingCount();
    retireQueue.collect(1);
    std::cout << "Vulkan objects retire staged=3 pending_before=" << pendingBefore
              << " pending_after=" << retireQueue.pendingCount() << '\n';
    if (pendingBefore != 3U || retireQueue.pendingCount() != 0U) {
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 1> frameBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    const std::array<VkDescriptorSetLayoutBinding, 1> drawBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto frameLayout = rb::vulkan::DescriptorSetLayout::create(device, frameBindings);
    auto drawLayout = rb::vulkan::DescriptorSetLayout::create(device, drawBindings);
    if (frameLayout == nullptr || drawLayout == nullptr) {
        return false;
    }
    const std::array<VkDescriptorPoolSize, 2> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    auto descriptorPool = rb::vulkan::DescriptorPool::create(device, 2, poolSizes);
    if (descriptorPool == nullptr) {
        return false;
    }
    const VkDescriptorSet frameSet = descriptorPool->allocate(frameLayout->handle());
    const VkDescriptorSet drawSet = descriptorPool->allocate(drawLayout->handle());
    if (frameSet == VK_NULL_HANDLE || drawSet == VK_NULL_HANDLE) {
        return false;
    }

    rb::SamplerConfig samplerConfig{};
    samplerConfig.minFilter = rb::TextureFilter::Nearest;
    samplerConfig.magFilter = rb::TextureFilter::Nearest;
    samplerConfig.mipFilter = rb::TextureFilter::Nearest;
    samplerConfig.address = rb::TextureAddress::ClampToEdge;
    auto sampler = rb::vulkan::Sampler::create(device, samplerConfig);
    if (sampler == nullptr) {
        return false;
    }

    VkDescriptorBufferInfo uniformInfo{uniformBuffer->handle(), 0, sizeof(frameTint)};
    VkDescriptorImageInfo textureInfo{sampler->handle(), texture->view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = frameSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uniformInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = drawSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &textureInfo;
    vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    const std::array<rb::vulkan::VertexAttribute, 3> attributes{
        rb::vulkan::VertexAttribute{0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        rb::vulkan::VertexAttribute{1, VK_FORMAT_R32G32_SFLOAT, 12},
        rb::vulkan::VertexAttribute{2, VK_FORMAT_R32G32B32A32_SFLOAT, 20}};
    const std::array<VkFormat, 1> colorFormats{VK_FORMAT_R8G8B8A8_UNORM};
    const std::array<VkDescriptorSetLayout, 2> setLayouts{frameLayout->handle(),
                                                          drawLayout->handle()};
    rb::vulkan::PipelineDescription pipelineDescription{};
    pipelineDescription.vertexCode = vertexCode;
    pipelineDescription.fragmentCode = fragmentCode;
    pipelineDescription.vertexStride = sizeof(Vertex);
    pipelineDescription.vertexAttributes = attributes;
    pipelineDescription.depthTest = true;
    pipelineDescription.depthWrite = true;
    pipelineDescription.colorFormats = colorFormats;
    pipelineDescription.depthFormat = VK_FORMAT_D32_SFLOAT;
    pipelineDescription.setLayouts = setLayouts;
    pipelineDescription.pushConstantBytes = static_cast<std::uint32_t>(sizeof(trianglePush));
    auto pipeline =
        rb::vulkan::Pipeline::create(device, pipelineDescription, pipelineCache->handle());
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
    colorAttachment.clearValue.color = {{0.0F, 0.0F, 0.0F, 1.0F}};
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthTarget->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = VkExtent2D{passExtent, passExtent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commands.buffer, &renderingInfo);

    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(passExtent),
                              static_cast<float>(passExtent), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, {passExtent, passExtent}};
    vkCmdSetViewport(commands.buffer, 0, 1, &viewport);
    vkCmdSetScissor(commands.buffer, 0, 1, &scissor);
    vkCmdBindPipeline(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
    const std::array<VkDescriptorSet, 2> sets{frameSet, drawSet};
    vkCmdBindDescriptorSets(commands.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout(),
                            0, static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
    const VkBuffer vertexHandle = vertexBuffer->handle();
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commands.buffer, 0, 1, &vertexHandle, &vertexOffset);
    vkCmdBindIndexBuffer(commands.buffer, indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(commands.buffer, pipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       static_cast<std::uint32_t>(sizeof(trianglePush)), trianglePush.data());
    vkCmdDraw(commands.buffer, 3, 1, 0, 0);
    vkCmdPushConstants(commands.buffer, pipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       static_cast<std::uint32_t>(sizeof(quadPush)), quadPush.data());
    vkCmdDrawIndexed(commands.buffer, static_cast<std::uint32_t>(indices.size()), 1, 0, 3, 0);
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
        std::vector<std::byte> colorPixels(
            static_cast<std::size_t>(passExtent) * passExtent * 4U);
        std::vector<std::byte> depthTexels(
            static_cast<std::size_t>(passExtent) * passExtent * 4U);
        if (!readback->readImage(*colorTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, colorPixels)) {
            return false;
        }
        if (!readback->readImage(*depthTarget, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 depthTexels)) {
            return false;
        }
        const bool colorsPassed = checkColors(colorPixels);
        const bool depthsPassed = checkDepths(depthTexels);
        checksPassed = colorsPassed && depthsPassed;
    }

    retireQueue.beginFrame(2);
    retireQueue.retire(std::move(vertexBuffer));
    retireQueue.retire(std::move(indexBuffer));
    retireQueue.retire(std::move(uniformBuffer));
    retireQueue.retire(std::move(texture));
    retireQueue.retire(std::move(colorTarget));
    retireQueue.retire(std::move(depthTarget));
    retireQueue.retire(std::move(sampler));
    const std::size_t retiredCount = retireQueue.pendingCount();
    retireQueue.collect(2);
    std::cout << "Vulkan objects retire final=" << retiredCount
              << " pending_after=" << retireQueue.pendingCount() << '\n';
    if (retiredCount != 7U || retireQueue.pendingCount() != 0U) {
        return false;
    }

    const rb::vulkan::AllocatorStats& stats = allocator->stats();
    std::cout << "Vulkan objects allocator blocks=" << stats.blockCount
              << " dedicated=" << stats.dedicatedCount << " live=" << stats.allocationCount
              << " reserved_bytes=" << stats.reservedBytes << " used_bytes=" << stats.usedBytes
              << '\n';
    if (stats.allocationCount != 0U) {
        return false;
    }

    if (!pipelineCache->save()) {
        return false;
    }
    std::cout << "Vulkan objects cache_saved bytes=" << pipelineCache->savedBytes() << '\n';
    return checksPassed;
}

}

std::vector<std::uint32_t> loadSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open SPIR-V file " << path << '\n';
        return {};
    }
    const std::streamsize size = static_cast<std::streamsize>(file.tellg());
    if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
        std::cerr << "SPIR-V file has an invalid size " << path << '\n';
        return {};
    }

    std::vector<std::uint32_t> code(
        static_cast<std::size_t>(size / static_cast<std::streamsize>(sizeof(std::uint32_t))));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
        std::cerr << "Failed to read SPIR-V file " << path << '\n';
        return {};
    }
    return code;
}

bool runObjectsPass(const rb::vulkan::Device& device, const ObjectsPassPaths& paths) {
    const bool passed = runChecks(device, paths);
    std::cout << "Vulkan objects summary status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
