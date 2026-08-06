#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace rb::vulkan {

class Device;
class FrameLoop;

class Swapchain {
public:
    static std::unique_ptr<Swapchain> create(const Device& device, VkSurfaceKHR surface,
                                             VkExtent2D requestedExtent);

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    [[nodiscard]] VkSwapchainKHR handle() const noexcept;
    [[nodiscard]] VkFormat format() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;
    [[nodiscard]] const std::vector<VkImage>& images() const noexcept;
    [[nodiscard]] const std::vector<VkImageView>& imageViews() const noexcept;

private:
    friend class FrameLoop;

    enum class ReplaceResult {
        complete,
        unavailable,
        fatal,
    };

    Swapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
              std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily) noexcept;
    [[nodiscard]] ReplaceResult recreate(VkExtent2D requestedExtent);
    [[nodiscard]] ReplaceResult replace(VkExtent2D requestedExtent);
    void destroy() noexcept;

    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    std::uint32_t m_graphicsQueueFamily{0};
    std::uint32_t m_presentQueueFamily{0};
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
};

}
