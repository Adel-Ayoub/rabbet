#pragma once

#include "rabbet/render/vulkan/Allocator.h"
#include "rabbet/render/vulkan/Buffer.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace rb::vulkan {

class Device;
class Image;

[[nodiscard]] std::uint32_t formatTexelBytes(VkFormat format) noexcept;

// The device and the allocator must outlive this object.
class Readback {
public:
    static std::unique_ptr<Readback> create(const Device& device, Allocator& allocator);

    Readback(const Readback&) = delete;
    Readback& operator=(const Readback&) = delete;
    ~Readback();

    // The selected mip is read in full.
    // destination.size() must equal its width times height times the format texel size.
    // currentLayout must name the image layout on entry and is restored before return.
    [[nodiscard]] bool readImage(const Image& image, VkImageLayout currentLayout,
                                 VkPipelineStageFlags2 currentStage, VkAccessFlags2 currentAccess,
                                 std::span<std::byte> destination,
                                 std::uint32_t mipLevel = 0,
                                 std::uint32_t arrayLayer = 0);
    // The offset and read extent use selected-mip texels.
    [[nodiscard]] bool readImageRegion(const Image& image, VkOffset2D offset,
                                       VkExtent2D readExtent,
                                       VkImageLayout currentLayout,
                                       VkPipelineStageFlags2 currentStage,
                                       VkAccessFlags2 currentAccess,
                                       std::span<std::byte> destination,
                                       std::uint32_t mipLevel = 0,
                                       std::uint32_t arrayLayer = 0);

private:
    Readback(const Device& device, VkQueue queue, Allocator& allocator,
             VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkFence fence) noexcept;

    [[nodiscard]] bool ensureBuffer(VkDeviceSize size);

    const Device* m_deviceObject{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    Allocator* m_allocator{nullptr};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};
    VkFence m_fence{VK_NULL_HANDLE};
    std::unique_ptr<Buffer> m_buffer;
};

}
