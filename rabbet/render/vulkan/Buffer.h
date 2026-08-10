#pragma once

#include "rabbet/render/vulkan/Allocator.h"

#include <vulkan/vulkan.h>

#include <memory>

namespace rb::vulkan {

class Device;

class Buffer {
public:
    static std::unique_ptr<Buffer> create(const Device& device, Allocator& allocator,
                                          VkDeviceSize size, VkBufferUsageFlags usage,
                                          MemoryClass memoryClass);

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer();

    [[nodiscard]] VkBuffer handle() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;
    [[nodiscard]] void* mapped() const noexcept;
    [[nodiscard]] bool flush(VkDeviceSize offset, VkDeviceSize size) noexcept;
    [[nodiscard]] bool invalidate(VkDeviceSize offset, VkDeviceSize size) noexcept;

private:
    Buffer(VkDevice device, Allocator& allocator, VkBuffer buffer,
           const Allocation& allocation, VkDeviceSize size) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    Allocator* m_allocator{nullptr};
    VkBuffer m_buffer{VK_NULL_HANDLE};
    Allocation m_allocation{};
    VkDeviceSize m_size{0};
};

}
