#include "rabbet/render/vulkan/Barriers.h"

#include <vector>

namespace rb::vulkan {

namespace {

VkImageMemoryBarrier2 toVkBarrier(const ImageBarrier& barrier) noexcept {
    VkImageMemoryBarrier2 result{};
    result.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    result.srcStageMask = barrier.srcStage;
    result.srcAccessMask = barrier.srcAccess;
    result.dstStageMask = barrier.dstStage;
    result.dstAccessMask = barrier.dstAccess;
    result.oldLayout = barrier.oldLayout;
    result.newLayout = barrier.newLayout;
    result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    result.image = barrier.image;
    result.subresourceRange.aspectMask = barrier.aspect;
    result.subresourceRange.baseMipLevel = barrier.baseMipLevel;
    result.subresourceRange.levelCount = barrier.mipLevels;
    result.subresourceRange.baseArrayLayer = barrier.baseArrayLayer;
    result.subresourceRange.layerCount = barrier.arrayLayers;
    return result;
}

VkBufferMemoryBarrier2 toVkBarrier(const BufferBarrier& barrier) noexcept {
    VkBufferMemoryBarrier2 result{};
    result.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    result.srcStageMask = barrier.srcStage;
    result.srcAccessMask = barrier.srcAccess;
    result.dstStageMask = barrier.dstStage;
    result.dstAccessMask = barrier.dstAccess;
    result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    result.buffer = barrier.buffer;
    result.offset = barrier.offset;
    result.size = barrier.size;
    return result;
}

}

void cmdBarriers(VkCommandBuffer commandBuffer, std::span<const ImageBarrier> imageBarriers,
                 std::span<const BufferBarrier> bufferBarriers) {
    std::vector<VkImageMemoryBarrier2> images;
    images.reserve(imageBarriers.size());
    for (const ImageBarrier& barrier : imageBarriers) {
        images.push_back(toVkBarrier(barrier));
    }
    std::vector<VkBufferMemoryBarrier2> buffers;
    buffers.reserve(bufferBarriers.size());
    for (const BufferBarrier& barrier : bufferBarriers) {
        buffers.push_back(toVkBarrier(barrier));
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(images.size());
    dependency.pImageMemoryBarriers = images.data();
    dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(buffers.size());
    dependency.pBufferMemoryBarriers = buffers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void cmdImageBarrier(VkCommandBuffer commandBuffer, const ImageBarrier& barrier) {
    const VkImageMemoryBarrier2 vkBarrier = toVkBarrier(barrier);
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &vkBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void cmdBufferBarrier(VkCommandBuffer commandBuffer, const BufferBarrier& barrier) {
    const VkBufferMemoryBarrier2 vkBarrier = toVkBarrier(barrier);
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &vkBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

}
