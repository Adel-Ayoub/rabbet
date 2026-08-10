#include "rabbet/render/vulkan/Readback.h"

#include "rabbet/render/vulkan/Barriers.h"
#include "rabbet/render/vulkan/Device.h"
#include "rabbet/render/vulkan/Image.h"

#include <cstdio>
#include <cstring>

namespace rb::vulkan {

namespace {

constexpr std::uint64_t readbackTimeoutNanoseconds = 5'000'000'000ULL;

}

std::uint32_t formatTexelBytes(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        default:
            return 0;
    }
}

std::unique_ptr<Readback> Readback::create(const Device& device, Allocator& allocator) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device.handle(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback command pool creation failed\n");
        return nullptr;
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.handle(), &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback command buffer allocation failed\n");
        vkDestroyCommandPool(device.handle(), commandPool, nullptr);
        return nullptr;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device.handle(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback fence creation failed\n");
        vkDestroyCommandPool(device.handle(), commandPool, nullptr);
        return nullptr;
    }
    return std::unique_ptr<Readback>(new Readback(device, device.graphicsQueue(), allocator,
                                                  commandPool, commandBuffer, fence));
}

Readback::Readback(const Device& device, VkQueue queue, Allocator& allocator,
                   VkCommandPool commandPool, VkCommandBuffer commandBuffer,
                   VkFence fence) noexcept
    : m_deviceObject(&device), m_device(device.handle()), m_queue(queue),
      m_allocator(&allocator), m_commandPool(commandPool), m_commandBuffer(commandBuffer),
      m_fence(fence) {}

Readback::~Readback() {
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_fence, nullptr);
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
}

bool Readback::ensureBuffer(VkDeviceSize size) {
    if (m_buffer != nullptr && m_buffer->size() >= size) {
        return true;
    }
    m_buffer.reset();
    m_buffer = Buffer::create(*m_deviceObject, *m_allocator, size,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryClass::hostReadback);
    return m_buffer != nullptr;
}

bool Readback::readImage(VkImage image, VkFormat format, VkExtent2D extent,
                         VkImageLayout currentLayout, VkPipelineStageFlags2 currentStage,
                         VkAccessFlags2 currentAccess, std::span<std::byte> destination) {
    const std::uint32_t texelBytes = formatTexelBytes(format);
    if (image == VK_NULL_HANDLE || texelBytes == 0U || extent.width == 0U ||
        extent.height == 0U || currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        std::fprintf(stderr, "Vulkan readback received an invalid request\n");
        return false;
    }
    const VkDeviceSize required = static_cast<VkDeviceSize>(extent.width) *
                                  static_cast<VkDeviceSize>(extent.height) *
                                  static_cast<VkDeviceSize>(texelBytes);
    if (destination.size() != required) {
        std::fprintf(stderr, "Vulkan readback destination size does not match the image\n");
        return false;
    }
    if (!ensureBuffer(required)) {
        return false;
    }

    if (vkResetCommandBuffer(m_commandBuffer, 0) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback command buffer reset failed\n");
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback command buffer begin failed\n");
        return false;
    }

    const VkImageAspectFlags aspect = formatAspect(format);
    ImageBarrier toTransfer{};
    toTransfer.image = image;
    toTransfer.srcStage = currentStage;
    toTransfer.srcAccess = currentAccess;
    toTransfer.dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
    toTransfer.oldLayout = currentLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.aspect = aspect;
    cmdImageBarrier(m_commandBuffer, toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = aspect;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = VkExtent3D{extent.width, extent.height, 1U};
    vkCmdCopyImageToBuffer(m_commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_buffer->handle(), 1, &region);

    ImageBarrier fromTransfer{};
    fromTransfer.image = image;
    fromTransfer.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    fromTransfer.srcAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
    fromTransfer.dstStage = currentStage;
    fromTransfer.dstAccess = currentAccess;
    fromTransfer.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    fromTransfer.newLayout = currentLayout;
    fromTransfer.aspect = aspect;
    BufferBarrier toHost{};
    toHost.buffer = m_buffer->handle();
    toHost.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    toHost.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStage = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccess = VK_ACCESS_2_HOST_READ_BIT;
    toHost.size = required;
    const ImageBarrier imageBarriers[]{fromTransfer};
    const BufferBarrier bufferBarriers[]{toHost};
    cmdBarriers(m_commandBuffer, imageBarriers, bufferBarriers);

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback command buffer end failed\n");
        return false;
    }
    if (vkResetFences(m_device, 1, &m_fence) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback fence reset failed\n");
        return false;
    }

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = m_commandBuffer;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    const VkResult submitResult = vkQueueSubmit2(m_queue, 1, &submitInfo, m_fence);
    if (submitResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback submission failed with result %d\n",
                     static_cast<int>(submitResult));
        return false;
    }
    const VkResult waitResult =
        vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, readbackTimeoutNanoseconds);
    if (waitResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan readback fence wait failed with result %d\n",
                     static_cast<int>(waitResult));
        static_cast<void>(vkDeviceWaitIdle(m_device));
        return false;
    }

    if (!m_buffer->invalidate(0, required)) {
        std::fprintf(stderr, "Vulkan readback memory invalidation failed\n");
        return false;
    }
    std::memcpy(destination.data(), m_buffer->mapped(), static_cast<std::size_t>(required));
    return true;
}

}
