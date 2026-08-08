#include "rabbet/render/vulkan/Swapchain.h"

#include "rabbet/render/vulkan/Device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace rb::vulkan {

namespace {

constexpr std::size_t enumerationAttempts = 4;

std::optional<std::vector<VkSurfaceFormatKHR>>
enumerateSurfaceFormats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr) !=
            VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkSurfaceFormatKHR> formats(count);
        const VkResult result =
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
        if (result == VK_SUCCESS) {
            formats.resize(count);
            return formats;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkPresentModeKHR>> enumeratePresentModes(VkPhysicalDevice physicalDevice,
                                                                   VkSurfaceKHR surface) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr) !=
            VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkPresentModeKHR> modes(count);
        const VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                                          &count, modes.data());
        if (result == VK_SUCCESS) {
            modes.resize(count);
            return modes;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkImage>> enumerateSwapchainImages(VkDevice device,
                                                             VkSwapchainKHR swapchain) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr) != VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkImage> images(count);
        const VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
        if (result == VK_SUCCESS) {
            images.resize(count);
            return images;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<VkSurfaceFormatKHR>
chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    constexpr VkFormat requiredFormat = VK_FORMAT_B8G8R8A8_UNORM;
    constexpr VkColorSpaceKHR requiredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    if (formats.size() == 1U && formats.front().format == VK_FORMAT_UNDEFINED &&
        formats.front().colorSpace == requiredColorSpace) {
        return VkSurfaceFormatKHR{requiredFormat, formats.front().colorSpace};
    }
    const auto match = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
        return format.format == requiredFormat && format.colorSpace == requiredColorSpace;
    });
    if (match == formats.end()) {
        return std::nullopt;
    }
    return *match;
}

std::optional<VkCompositeAlphaFlagBitsKHR>
chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array candidates{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const VkCompositeAlphaFlagBitsKHR candidate : candidates) {
        if ((supported & static_cast<VkCompositeAlphaFlagsKHR>(candidate)) != 0U) {
            return candidate;
        }
    }
    return std::nullopt;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D requestedExtent) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    return {
        std::clamp(requestedExtent.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(requestedExtent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
}

void destroyImageViews(VkDevice device, const std::vector<VkImageView>& views) {
    for (const VkImageView view : views) {
        vkDestroyImageView(device, view, nullptr);
    }
}

}

std::unique_ptr<Swapchain> Swapchain::create(const Device& device, VkSurfaceKHR surface,
                                             VkExtent2D requestedExtent) {
    auto swapchain = std::unique_ptr<Swapchain>(
        new Swapchain(device.physicalHandle(), device.handle(), surface,
                      device.graphicsQueueFamily(), device.presentQueueFamily()));
    if (swapchain->replace(requestedExtent) != ReplaceResult::complete) {
        return nullptr;
    }
    return swapchain;
}

Swapchain::Swapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
                     std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily) noexcept
    : m_physicalDevice(physicalDevice), m_device(device), m_surface(surface),
      m_graphicsQueueFamily(graphicsQueueFamily), m_presentQueueFamily(presentQueueFamily) {}

Swapchain::~Swapchain() {
    destroy();
}

Swapchain::ReplaceResult Swapchain::recreate(VkExtent2D requestedExtent) {
    return replace(requestedExtent);
}

Swapchain::ReplaceResult Swapchain::replace(VkExtent2D requestedExtent) {
    if (requestedExtent.width == 0U || requestedExtent.height == 0U) {
        return ReplaceResult::unavailable;
    }
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan surface capability query failed\n");
        return ReplaceResult::fatal;
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0U) {
        std::fprintf(stderr, "Vulkan surface cannot be used as a color attachment\n");
        return ReplaceResult::fatal;
    }

    const auto formats = enumerateSurfaceFormats(m_physicalDevice, m_surface);
    if (!formats.has_value() || formats->empty()) {
        std::fprintf(stderr, "Vulkan surface format query failed\n");
        return ReplaceResult::fatal;
    }
    const auto surfaceFormat = chooseSurfaceFormat(*formats);
    if (!surfaceFormat.has_value()) {
        std::fprintf(stderr, "Vulkan surface lacks the required UNORM format\n");
        return ReplaceResult::fatal;
    }

    const auto presentModes = enumeratePresentModes(m_physicalDevice, m_surface);
    if (!presentModes.has_value() || presentModes->empty()) {
        std::fprintf(stderr, "Vulkan present mode query failed\n");
        return ReplaceResult::fatal;
    }
    if (std::find(presentModes->begin(), presentModes->end(), VK_PRESENT_MODE_FIFO_KHR) ==
        presentModes->end()) {
        std::fprintf(stderr, "Vulkan FIFO present mode is unavailable\n");
        return ReplaceResult::fatal;
    }

    const auto compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
    if (!compositeAlpha.has_value()) {
        std::fprintf(stderr, "Vulkan surface has no supported composite alpha mode\n");
        return ReplaceResult::fatal;
    }

    const std::uint32_t desiredImageCount =
        capabilities.minImageCount == std::numeric_limits<std::uint32_t>::max()
            ? capabilities.minImageCount
            : capabilities.minImageCount + 1U;
    const std::uint32_t imageCount = capabilities.maxImageCount == 0U
                                         ? desiredImageCount
                                         : std::min(desiredImageCount, capabilities.maxImageCount);
    const VkExtent2D newExtent = chooseExtent(capabilities, requestedExtent);
    if (newExtent.width == 0U || newExtent.height == 0U) {
        return ReplaceResult::unavailable;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat->format;
    createInfo.imageColorSpace = surfaceFormat->colorSpace;
    createInfo.imageExtent = newExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array queueFamilies{m_graphicsQueueFamily, m_presentQueueFamily};
    if (m_graphicsQueueFamily == m_presentQueueFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilies.size());
        createInfo.pQueueFamilyIndices = queueFamilies.data();
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = *compositeAlpha;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = m_swapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    const VkResult swapchainResult =
        vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &newSwapchain);
    if (createInfo.oldSwapchain != VK_NULL_HANDLE) {
        destroy();
    }
    if (swapchainResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan swapchain creation failed with result %d\n",
                     static_cast<int>(swapchainResult));
        return ReplaceResult::fatal;
    }

    auto newImages = enumerateSwapchainImages(m_device, newSwapchain);
    if (!newImages.has_value() || newImages->empty()) {
        std::fprintf(stderr, "Vulkan swapchain image query failed\n");
        vkDestroySwapchainKHR(m_device, newSwapchain, nullptr);
        return ReplaceResult::fatal;
    }

    std::vector<VkImageView> newImageViews;
    newImageViews.reserve(newImages->size());
    for (const VkImage image : *newImages) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat->format;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            std::fprintf(stderr, "Vulkan swapchain image view creation failed\n");
            destroyImageViews(m_device, newImageViews);
            vkDestroySwapchainKHR(m_device, newSwapchain, nullptr);
            return ReplaceResult::fatal;
        }
        newImageViews.push_back(view);
    }

    m_swapchain = newSwapchain;
    m_format = surfaceFormat->format;
    m_extent = newExtent;
    m_images = std::move(*newImages);
    m_imageViews = std::move(newImageViews);
    return ReplaceResult::complete;
}

void Swapchain::destroy() noexcept {
    destroyImageViews(m_device, m_imageViews);
    m_imageViews.clear();
    m_images.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    m_format = VK_FORMAT_UNDEFINED;
    m_extent = {};
}

VkSwapchainKHR Swapchain::handle() const noexcept {
    return m_swapchain;
}

VkFormat Swapchain::format() const noexcept {
    return m_format;
}

VkExtent2D Swapchain::extent() const noexcept {
    return m_extent;
}

const std::vector<VkImage>& Swapchain::images() const noexcept {
    return m_images;
}

const std::vector<VkImageView>& Swapchain::imageViews() const noexcept {
    return m_imageViews;
}

}
