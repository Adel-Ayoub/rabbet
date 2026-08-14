#include "rabbet/render/vulkan/OffscreenTarget.h"

#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Device.h"
#include "rabbet/render/vulkan/Readback.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace rb::vulkan {

std::unique_ptr<OffscreenTarget> OffscreenTarget::create(
    const Device& device, Allocator& allocator,
    const OffscreenTargetDescription& description) {
    const bool hasColor = description.colorFormat != VK_FORMAT_UNDEFINED;
    if (description.extent.width == 0U || description.extent.height == 0U ||
        (!hasColor && !description.depth) ||
        (hasColor && formatAspect(description.colorFormat) != VK_IMAGE_ASPECT_COLOR_BIT) ||
        (description.sampledColor && !hasColor) ||
        (description.sampledDepth && !description.depth)) {
        std::fprintf(stderr, "Vulkan offscreen target received an invalid description\n");
        return nullptr;
    }

    std::unique_ptr<Image> color;
    if (hasColor) {
        ImageDescription imageDescription{};
        imageDescription.extent = description.extent;
        imageDescription.format = description.colorFormat;
        imageDescription.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (description.sampledColor) {
            imageDescription.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        color = Image::create(device, allocator, imageDescription);
        if (color == nullptr) {
            return nullptr;
        }
    }

    std::unique_ptr<Image> depth;
    if (description.depth) {
        ImageDescription imageDescription{};
        imageDescription.extent = description.extent;
        imageDescription.format = VK_FORMAT_D32_SFLOAT;
        imageDescription.usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (description.sampledDepth) {
            imageDescription.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        depth = Image::create(device, allocator, imageDescription);
        if (depth == nullptr) {
            return nullptr;
        }
    }

    return std::unique_ptr<OffscreenTarget>(new OffscreenTarget(
        description.extent, std::move(color), std::move(depth)));
}

OffscreenTarget::OffscreenTarget(VkExtent2D extent, std::unique_ptr<Image> color,
                                 std::unique_ptr<Image> depth) noexcept
    : m_extent(extent), m_color(std::move(color)), m_depth(std::move(depth)) {}

Image* OffscreenTarget::color() noexcept {
    return m_color.get();
}

const Image* OffscreenTarget::color() const noexcept {
    return m_color.get();
}

Image* OffscreenTarget::depth() noexcept {
    return m_depth.get();
}

const Image* OffscreenTarget::depth() const noexcept {
    return m_depth.get();
}

VkExtent2D OffscreenTarget::extent() const noexcept {
    return m_extent;
}

bool OffscreenTarget::readColor(Readback& readback, VkImageLayout currentLayout,
                                VkPipelineStageFlags2 currentStage,
                                VkAccessFlags2 currentAccess,
                                std::span<std::byte> destination) const {
    if (m_color == nullptr) {
        std::fprintf(stderr, "Vulkan offscreen color read received no color image\n");
        return false;
    }
    return readback.readImage(*m_color, currentLayout, currentStage, currentAccess,
                              destination);
}

bool OffscreenTarget::readDepth(Readback& readback, VkImageLayout currentLayout,
                                VkPipelineStageFlags2 currentStage,
                                VkAccessFlags2 currentAccess,
                                std::span<std::byte> destination) const {
    if (m_depth == nullptr) {
        std::fprintf(stderr, "Vulkan offscreen depth read received no depth image\n");
        return false;
    }
    return readback.readImage(*m_depth, currentLayout, currentStage, currentAccess,
                              destination);
}

bool OffscreenTarget::readPickId(Readback& readback, std::int32_t x, std::int32_t y,
                                 VkImageLayout currentLayout,
                                 VkPipelineStageFlags2 currentStage,
                                 VkAccessFlags2 currentAccess,
                                 std::int32_t& value) const {
    value = noPickId;
    if (m_color == nullptr || m_color->format() != VK_FORMAT_R32_SINT) {
        std::fprintf(stderr, "Vulkan pick read received no signed integer image\n");
        return false;
    }
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= m_extent.width ||
        static_cast<std::uint32_t>(y) >= m_extent.height) {
        return true;
    }

    std::array<std::byte, sizeof(value)> bytes{};
    if (!readback.readImageRegion(*m_color, VkOffset2D{x, y}, VkExtent2D{1, 1},
                                  currentLayout, currentStage, currentAccess, bytes)) {
        return false;
    }
    std::memcpy(&value, bytes.data(), sizeof(value));
    return true;
}

}
