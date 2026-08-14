#pragma once

#include "rabbet/render/vulkan/Allocator.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace rb::vulkan {

class Device;

struct ImageDescription {
    VkExtent2D extent{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageUsageFlags usage{0};
    std::uint32_t mipLevels{1};
    std::uint32_t arrayLayers{1};
    bool cube{false};
};

[[nodiscard]] VkImageAspectFlags formatAspect(VkFormat format) noexcept;

class Image {
public:
    static std::unique_ptr<Image> create(const Device& device, Allocator& allocator,
                                         const ImageDescription& description);

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    ~Image();

    [[nodiscard]] VkImage handle() const noexcept;
    [[nodiscard]] VkImageView view() const noexcept;
    [[nodiscard]] VkImageView layerView(std::uint32_t layer,
                                        std::uint32_t mipLevel = 0) const noexcept;
    [[nodiscard]] VkFormat format() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;
    [[nodiscard]] std::uint32_t mipLevels() const noexcept;
    [[nodiscard]] std::uint32_t arrayLayers() const noexcept;
    [[nodiscard]] VkImageAspectFlags aspect() const noexcept;

private:
    Image(VkDevice device, Allocator& allocator, VkImage image, VkImageView view,
          std::vector<VkImageView> layerViews, const Allocation& allocation,
          const ImageDescription& description) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    Allocator* m_allocator{nullptr};
    VkImage m_image{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    std::vector<VkImageView> m_layerViews;
    Allocation m_allocation{};
    VkFormat m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    std::uint32_t m_mipLevels{1};
    std::uint32_t m_arrayLayers{1};
};

}
