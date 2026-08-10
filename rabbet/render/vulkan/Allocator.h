#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rb::vulkan {

class Device;

enum class MemoryClass {
    deviceOnly,
    hostUpload,
    hostReadback,
};

struct Allocation {
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    void* mapped{nullptr};
    std::uint32_t memoryType{0};
    std::uint32_t blockId{0};
    bool coherent{false};
};

struct AllocatorStats {
    std::uint32_t blockCount{0};
    std::uint32_t dedicatedCount{0};
    std::uint32_t allocationCount{0};
    VkDeviceSize reservedBytes{0};
    VkDeviceSize usedBytes{0};
};

class Allocator {
public:
    static std::unique_ptr<Allocator> create(const Device& device);

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;
    ~Allocator();

    [[nodiscard]] std::optional<Allocation> allocate(const VkMemoryRequirements& requirements,
                                                     MemoryClass memoryClass, bool linear);
    void free(const Allocation& allocation) noexcept;

    [[nodiscard]] bool flush(const Allocation& allocation, VkDeviceSize offset,
                             VkDeviceSize size) noexcept;
    [[nodiscard]] bool invalidate(const Allocation& allocation, VkDeviceSize offset,
                                  VkDeviceSize size) noexcept;

    [[nodiscard]] const AllocatorStats& stats() const noexcept;

private:
    struct FreeRange {
        VkDeviceSize offset{0};
        VkDeviceSize size{0};
    };

    struct Block {
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{0};
        VkDeviceSize usedBytes{0};
        void* mapped{nullptr};
        std::uint32_t memoryType{0};
        std::uint32_t id{0};
        bool linear{false};
        std::vector<FreeRange> freeRanges;
    };

    struct Dedicated {
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{0};
        void* mapped{nullptr};
        std::uint32_t id{0};
    };

    Allocator(VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProperties,
              VkDeviceSize nonCoherentAtomSize) noexcept;

    [[nodiscard]] std::optional<Allocation> allocateFromType(
        const VkMemoryRequirements& requirements, std::uint32_t memoryType, bool linear);
    [[nodiscard]] std::optional<Allocation> allocateDedicated(VkDeviceSize size,
                                                              std::uint32_t memoryType);
    [[nodiscard]] bool suballocate(Block& block, VkDeviceSize size, VkDeviceSize alignment,
                                   VkDeviceSize& outOffset);
    [[nodiscard]] VkDeviceSize blockSizeForType(std::uint32_t memoryType) const noexcept;
    [[nodiscard]] bool hostRange(const Allocation& allocation, VkDeviceSize offset,
                                 VkDeviceSize size, VkMappedMemoryRange& range) const noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};
    VkDeviceSize m_nonCoherentAtomSize{1};
    std::vector<Block> m_blocks;
    std::vector<Dedicated> m_dedicated;
    std::uint32_t m_nextId{1};
    AllocatorStats m_stats{};
};

}
