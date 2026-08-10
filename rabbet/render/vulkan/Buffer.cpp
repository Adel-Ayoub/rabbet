#include "rabbet/render/vulkan/Buffer.h"

#include "rabbet/render/vulkan/Device.h"

#include <cstdio>

namespace rb::vulkan {

std::unique_ptr<Buffer> Buffer::create(const Device& device, Allocator& allocator,
                                       VkDeviceSize size, VkBufferUsageFlags usage,
                                       MemoryClass memoryClass) {
    if (size == 0U) {
        std::fprintf(stderr, "Vulkan buffer creation of zero bytes was requested\n");
        return nullptr;
    }

    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(device.handle(), &createInfo, nullptr, &buffer) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan buffer creation failed\n");
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.handle(), buffer, &requirements);
    auto allocation = allocator.allocate(requirements, memoryClass, true);
    if (!allocation.has_value()) {
        vkDestroyBuffer(device.handle(), buffer, nullptr);
        return nullptr;
    }
    if (vkBindBufferMemory(device.handle(), buffer, allocation->memory, allocation->offset) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan buffer memory binding failed\n");
        allocator.free(*allocation);
        vkDestroyBuffer(device.handle(), buffer, nullptr);
        return nullptr;
    }
    return std::unique_ptr<Buffer>(
        new Buffer(device.handle(), allocator, buffer, *allocation, size));
}

Buffer::Buffer(VkDevice device, Allocator& allocator, VkBuffer buffer,
               const Allocation& allocation, VkDeviceSize size) noexcept
    : m_device(device), m_allocator(&allocator), m_buffer(buffer), m_allocation(allocation),
      m_size(size) {}

Buffer::~Buffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    }
    if (m_allocator != nullptr) {
        m_allocator->free(m_allocation);
    }
}

VkBuffer Buffer::handle() const noexcept {
    return m_buffer;
}

VkDeviceSize Buffer::size() const noexcept {
    return m_size;
}

void* Buffer::mapped() const noexcept {
    return m_allocation.mapped;
}

bool Buffer::flush(VkDeviceSize offset, VkDeviceSize size) noexcept {
    return m_allocator->flush(m_allocation, offset, size);
}

bool Buffer::invalidate(VkDeviceSize offset, VkDeviceSize size) noexcept {
    return m_allocator->invalidate(m_allocation, offset, size);
}

}
