#include "rabbet/render/vulkan/Image.h"

#include "rabbet/render/vulkan/Device.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace rb::vulkan {

VkImageAspectFlags formatAspect(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

std::unique_ptr<Image> Image::create(const Device& device, Allocator& allocator,
                                     const ImageDescription& description) {
    // Combined depth and stencil formats are rejected because the engine dropped stencil,
    // and a single-view image could not serve them as sampled depth anyway.
    if (description.extent.width == 0U || description.extent.height == 0U ||
        description.format == VK_FORMAT_UNDEFINED || description.usage == 0U ||
        description.mipLevels == 0U ||
        description.arrayLayers == 0U ||
        formatAspect(description.format) ==
            (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) ||
        (description.cube &&
         (description.arrayLayers != 6U ||
          description.extent.width != description.extent.height))) {
        std::fprintf(stderr, "Vulkan image creation received an invalid description\n");
        return nullptr;
    }

    const VkImageCreateFlags flags =
        description.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0U;
    VkImageFormatProperties formatProperties{};
    const VkResult formatResult = vkGetPhysicalDeviceImageFormatProperties(
        device.physicalHandle(), description.format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, description.usage, flags, &formatProperties);
    if (formatResult != VK_SUCCESS || description.extent.width > formatProperties.maxExtent.width ||
        description.extent.height > formatProperties.maxExtent.height ||
        description.mipLevels > formatProperties.maxMipLevels ||
        description.arrayLayers > formatProperties.maxArrayLayers) {
        std::fprintf(stderr, "Vulkan image format does not support the requested description\n");
        return nullptr;
    }

    std::vector<VkImageView> layerViews;
    if (description.cube) {
        layerViews.reserve(static_cast<std::size_t>(description.arrayLayers) *
                           description.mipLevels);
    }

    VkImageCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createInfo.flags = flags;
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = description.format;
    createInfo.extent = VkExtent3D{description.extent.width, description.extent.height, 1U};
    createInfo.mipLevels = description.mipLevels;
    createInfo.arrayLayers = description.arrayLayers;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = description.usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device.handle(), &createInfo, nullptr, &image) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan image creation failed\n");
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.handle(), image, &requirements);
    auto allocation = allocator.allocate(requirements, MemoryClass::deviceOnly, false);
    if (!allocation.has_value()) {
        vkDestroyImage(device.handle(), image, nullptr);
        return nullptr;
    }
    if (vkBindImageMemory(device.handle(), image, allocation->memory, allocation->offset) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan image memory binding failed\n");
        vkDestroyImage(device.handle(), image, nullptr);
        allocator.free(*allocation);
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    if (description.cube) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (description.arrayLayers > 1U) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    }
    viewInfo.format = description.format;
    viewInfo.subresourceRange.aspectMask = formatAspect(description.format);
    viewInfo.subresourceRange.levelCount = description.mipLevels;
    viewInfo.subresourceRange.layerCount = description.arrayLayers;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device.handle(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan image view creation failed\n");
        vkDestroyImage(device.handle(), image, nullptr);
        allocator.free(*allocation);
        return nullptr;
    }

    if (description.cube) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        for (std::uint32_t layer = 0; layer < description.arrayLayers; ++layer) {
            viewInfo.subresourceRange.baseArrayLayer = layer;
            for (std::uint32_t mipLevel = 0; mipLevel < description.mipLevels; ++mipLevel) {
                viewInfo.subresourceRange.baseMipLevel = mipLevel;
                VkImageView layerView = VK_NULL_HANDLE;
                if (vkCreateImageView(device.handle(), &viewInfo, nullptr, &layerView) !=
                    VK_SUCCESS) {
                    std::fprintf(stderr, "Vulkan image layer view creation failed\n");
                    for (VkImageView createdView : layerViews) {
                        vkDestroyImageView(device.handle(), createdView, nullptr);
                    }
                    vkDestroyImageView(device.handle(), view, nullptr);
                    vkDestroyImage(device.handle(), image, nullptr);
                    allocator.free(*allocation);
                    return nullptr;
                }
                layerViews.push_back(layerView);
            }
        }
    }
    return std::unique_ptr<Image>(
        new Image(device.handle(), allocator, image, view, std::move(layerViews), *allocation,
                  description));
}

Image::Image(VkDevice device, Allocator& allocator, VkImage image, VkImageView view,
             std::vector<VkImageView> layerViews, const Allocation& allocation,
             const ImageDescription& description) noexcept
    : m_device(device), m_allocator(&allocator), m_image(image), m_view(view),
      m_layerViews(std::move(layerViews)), m_allocation(allocation),
      m_format(description.format), m_extent(description.extent),
      m_mipLevels(description.mipLevels), m_arrayLayers(description.arrayLayers) {}

Image::~Image() {
    for (VkImageView layerView : m_layerViews) {
        vkDestroyImageView(m_device, layerView, nullptr);
    }
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_view, nullptr);
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
    }
    if (m_allocator != nullptr) {
        m_allocator->free(m_allocation);
    }
}

VkImage Image::handle() const noexcept {
    return m_image;
}

VkImageView Image::view() const noexcept {
    return m_view;
}

VkImageView Image::layerView(std::uint32_t layer, std::uint32_t mipLevel) const noexcept {
    if (layer >= m_arrayLayers || mipLevel >= m_mipLevels || m_layerViews.empty()) {
        return VK_NULL_HANDLE;
    }
    return m_layerViews[static_cast<std::size_t>(layer) * m_mipLevels + mipLevel];
}

VkFormat Image::format() const noexcept {
    return m_format;
}

VkExtent2D Image::extent() const noexcept {
    return m_extent;
}

std::uint32_t Image::mipLevels() const noexcept {
    return m_mipLevels;
}

std::uint32_t Image::arrayLayers() const noexcept {
    return m_arrayLayers;
}

VkImageAspectFlags Image::aspect() const noexcept {
    return formatAspect(m_format);
}

}
