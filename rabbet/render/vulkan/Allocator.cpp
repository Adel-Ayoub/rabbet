#include "rabbet/render/vulkan/Allocator.h"

#include "rabbet/render/vulkan/Device.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <span>

namespace rb::vulkan {

namespace {

constexpr VkDeviceSize standardBlockSize = 64ULL * 1024ULL * 1024ULL;
constexpr VkDeviceSize minimumBlockSize = 1ULL * 1024ULL * 1024ULL;

std::span<const VkMemoryPropertyFlags> propertyChain(MemoryClass memoryClass) {
    static constexpr std::array<VkMemoryPropertyFlags, 2> deviceOnly{
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
    };
    static constexpr std::array<VkMemoryPropertyFlags, 3> hostUpload{
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    static constexpr std::array<VkMemoryPropertyFlags, 4> hostReadback{
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    switch (memoryClass) {
        case MemoryClass::deviceOnly:
            return deviceOnly;
        case MemoryClass::hostUpload:
            return hostUpload;
        case MemoryClass::hostReadback:
            return hostReadback;
    }
    return deviceOnly;
}

bool alignUp(VkDeviceSize value, VkDeviceSize alignment, VkDeviceSize& aligned) noexcept {
    if (alignment == 0U) {
        alignment = 1U;
    }
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0U) {
        aligned = value;
        return true;
    }
    const VkDeviceSize padding = alignment - remainder;
    if (value > std::numeric_limits<VkDeviceSize>::max() - padding) {
        return false;
    }
    aligned = value + padding;
    return true;
}

}

std::unique_ptr<Allocator> Allocator::create(const Device& device) {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.physicalHandle(), &memoryProperties);
    if (memoryProperties.memoryTypeCount == 0U) {
        std::fprintf(stderr, "Vulkan reported no memory types\n");
        return nullptr;
    }
    const VkDeviceSize atom = device.properties().limits.nonCoherentAtomSize;
    return std::unique_ptr<Allocator>(
        new Allocator(device.handle(), memoryProperties, atom == 0U ? 1U : atom));
}

Allocator::Allocator(VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProperties,
                     VkDeviceSize nonCoherentAtomSize) noexcept
    : m_device(device), m_memoryProperties(memoryProperties),
      m_nonCoherentAtomSize(nonCoherentAtomSize) {}

Allocator::~Allocator() {
    if (m_stats.allocationCount != 0U) {
        std::fprintf(stderr, "Vulkan allocator destroyed with %u live allocations\n",
                     m_stats.allocationCount);
    }
    for (const Dedicated& dedicated : m_dedicated) {
        vkFreeMemory(m_device, dedicated.memory, nullptr);
    }
    for (const Block& block : m_blocks) {
        vkFreeMemory(m_device, block.memory, nullptr);
    }
}

VkDeviceSize Allocator::blockSizeForType(std::uint32_t memoryType) const noexcept {
    const std::uint32_t heapIndex = m_memoryProperties.memoryTypes[memoryType].heapIndex;
    const VkDeviceSize heapSize = m_memoryProperties.memoryHeaps[heapIndex].size;
    const VkDeviceSize heapBound = heapSize / 8U;
    return std::clamp(heapBound, minimumBlockSize, standardBlockSize);
}

bool Allocator::suballocate(Block& block, VkDeviceSize size, VkDeviceSize alignment,
                            VkDeviceSize& outOffset) {
    for (std::size_t index = 0; index < block.freeRanges.size(); ++index) {
        const FreeRange range = block.freeRanges[index];
        VkDeviceSize alignedOffset = 0;
        if (!alignUp(range.offset, alignment, alignedOffset)) {
            continue;
        }
        const VkDeviceSize rangeEnd = range.offset + range.size;
        if (alignedOffset > rangeEnd || rangeEnd - alignedOffset < size) {
            continue;
        }

        const FreeRange front{range.offset, alignedOffset - range.offset};
        const FreeRange back{alignedOffset + size, rangeEnd - (alignedOffset + size)};
        auto position = block.freeRanges.erase(
            block.freeRanges.begin() + static_cast<std::ptrdiff_t>(index));
        if (back.size > 0U) {
            position = block.freeRanges.insert(position, back);
        }
        if (front.size > 0U) {
            block.freeRanges.insert(position, front);
        }
        block.usedBytes += size;
        outOffset = alignedOffset;
        return true;
    }
    return false;
}

std::optional<Allocation> Allocator::allocateDedicated(VkDeviceSize size,
                                                       std::uint32_t memoryType) {
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = size;
    allocateInfo.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        return std::nullopt;
    }

    const VkMemoryPropertyFlags flags =
        m_memoryProperties.memoryTypes[memoryType].propertyFlags;
    void* mapped = nullptr;
    if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U &&
        vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, memory, nullptr);
        return std::nullopt;
    }

    Dedicated dedicated{memory, size, mapped, m_nextId++};
    m_dedicated.push_back(dedicated);
    ++m_stats.dedicatedCount;
    ++m_stats.allocationCount;
    m_stats.reservedBytes += size;
    m_stats.usedBytes += size;

    Allocation allocation{};
    allocation.memory = memory;
    allocation.offset = 0;
    allocation.size = size;
    allocation.mapped = mapped;
    allocation.memoryType = memoryType;
    allocation.blockId = dedicated.id;
    allocation.coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
    return allocation;
}

std::optional<Allocation> Allocator::allocateFromType(const VkMemoryRequirements& requirements,
                                                      std::uint32_t memoryType, bool linear) {
    const VkMemoryPropertyFlags flags =
        m_memoryProperties.memoryTypes[memoryType].propertyFlags;
    const bool hostVisible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
    const bool coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;

    // Non-coherent host memory is placed and sized on atom boundaries so a flushed or
    // invalidated range can never cover a neighboring suballocation's bytes.
    VkDeviceSize size = requirements.size;
    VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, 1U);
    if (hostVisible && !coherent) {
        alignment = std::max(alignment, m_nonCoherentAtomSize);
        if (!alignUp(size, m_nonCoherentAtomSize, size)) {
            return std::nullopt;
        }
    }

    const VkDeviceSize blockSize = blockSizeForType(memoryType);
    if (size > blockSize / 2U) {
        return allocateDedicated(size, memoryType);
    }

    for (Block& block : m_blocks) {
        if (block.memoryType != memoryType || block.linear != linear) {
            continue;
        }
        VkDeviceSize offset = 0;
        if (suballocate(block, size, alignment, offset)) {
            ++m_stats.allocationCount;
            m_stats.usedBytes += size;
            Allocation allocation{};
            allocation.memory = block.memory;
            allocation.offset = offset;
            allocation.size = size;
            allocation.mapped =
                block.mapped == nullptr
                    ? nullptr
                    : static_cast<void*>(static_cast<std::byte*>(block.mapped) + offset);
            allocation.memoryType = memoryType;
            allocation.blockId = block.id;
            allocation.coherent = coherent;
            return allocation;
        }
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = blockSize;
    allocateInfo.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        return std::nullopt;
    }
    void* mapped = nullptr;
    if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U &&
        vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, memory, nullptr);
        return std::nullopt;
    }

    Block block{};
    block.memory = memory;
    block.size = blockSize;
    block.mapped = mapped;
    block.memoryType = memoryType;
    block.id = m_nextId++;
    block.linear = linear;
    block.freeRanges.push_back(FreeRange{0, blockSize});
    m_blocks.push_back(std::move(block));
    ++m_stats.blockCount;
    m_stats.reservedBytes += blockSize;

    VkDeviceSize offset = 0;
    if (!suballocate(m_blocks.back(), size, alignment, offset)) {
        return std::nullopt;
    }
    ++m_stats.allocationCount;
    m_stats.usedBytes += size;
    Allocation allocation{};
    allocation.memory = memory;
    allocation.offset = offset;
    allocation.size = size;
    allocation.mapped = mapped == nullptr
                            ? nullptr
                            : static_cast<void*>(static_cast<std::byte*>(mapped) + offset);
    allocation.memoryType = memoryType;
    allocation.blockId = m_blocks.back().id;
    allocation.coherent = coherent;
    return allocation;
}

std::optional<Allocation> Allocator::allocate(const VkMemoryRequirements& requirements,
                                              MemoryClass memoryClass, bool linear) {
    if (requirements.size == 0U) {
        std::fprintf(stderr, "Vulkan memory allocation of zero bytes was requested\n");
        return std::nullopt;
    }

    for (const VkMemoryPropertyFlags required : propertyChain(memoryClass)) {
        for (std::uint32_t type = 0; type < m_memoryProperties.memoryTypeCount; ++type) {
            if ((requirements.memoryTypeBits & (1U << type)) == 0U) {
                continue;
            }
            const VkMemoryPropertyFlags flags =
                m_memoryProperties.memoryTypes[type].propertyFlags;
            if ((flags & required) != required) {
                continue;
            }
            if (memoryClass != MemoryClass::deviceOnly &&
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0U) {
                continue;
            }
            auto allocation = allocateFromType(requirements, type, linear);
            if (allocation.has_value()) {
                return allocation;
            }
        }
    }
    std::fprintf(stderr, "Vulkan memory allocation of %llu bytes found no usable memory type\n",
                 static_cast<unsigned long long>(requirements.size));
    return std::nullopt;
}

void Allocator::free(const Allocation& allocation) noexcept {
    if (allocation.memory == VK_NULL_HANDLE) {
        return;
    }

    for (std::size_t index = 0; index < m_dedicated.size(); ++index) {
        if (m_dedicated[index].id != allocation.blockId) {
            continue;
        }
        vkFreeMemory(m_device, m_dedicated[index].memory, nullptr);
        m_stats.reservedBytes -= m_dedicated[index].size;
        m_stats.usedBytes -= m_dedicated[index].size;
        --m_stats.dedicatedCount;
        --m_stats.allocationCount;
        m_dedicated.erase(m_dedicated.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }

    for (Block& block : m_blocks) {
        if (block.id != allocation.blockId) {
            continue;
        }
        const auto position = std::lower_bound(
            block.freeRanges.begin(), block.freeRanges.end(), allocation.offset,
            [](const FreeRange& range, VkDeviceSize offset) { return range.offset < offset; });
        const bool overlapsPrevious =
            position != block.freeRanges.begin() &&
            (position - 1)->offset + (position - 1)->size > allocation.offset;
        const bool overlapsNext = position != block.freeRanges.end() &&
                                  allocation.offset + allocation.size > position->offset;
        if (allocation.size == 0U || allocation.offset + allocation.size > block.size ||
            overlapsPrevious || overlapsNext) {
            std::fprintf(stderr, "Vulkan allocator rejected an invalid or repeated free\n");
            return;
        }
        const auto inserted = block.freeRanges.insert(
            position, FreeRange{allocation.offset, allocation.size});
        const std::size_t insertedIndex =
            static_cast<std::size_t>(inserted - block.freeRanges.begin());
        if (insertedIndex + 1U < block.freeRanges.size()) {
            FreeRange& next = block.freeRanges[insertedIndex + 1U];
            if (inserted->offset + inserted->size == next.offset) {
                inserted->size += next.size;
                block.freeRanges.erase(block.freeRanges.begin() +
                                       static_cast<std::ptrdiff_t>(insertedIndex + 1U));
            }
        }
        if (insertedIndex > 0U) {
            FreeRange& previous = block.freeRanges[insertedIndex - 1U];
            FreeRange& current = block.freeRanges[insertedIndex];
            if (previous.offset + previous.size == current.offset) {
                previous.size += current.size;
                block.freeRanges.erase(block.freeRanges.begin() +
                                       static_cast<std::ptrdiff_t>(insertedIndex));
            }
        }
        block.usedBytes -= allocation.size;
        m_stats.usedBytes -= allocation.size;
        --m_stats.allocationCount;
        return;
    }
    std::fprintf(stderr, "Vulkan allocator was asked to free an unknown allocation\n");
}

bool Allocator::hostRange(const Allocation& allocation, VkDeviceSize offset, VkDeviceSize size,
                          VkMappedMemoryRange& range) const noexcept {
    if (allocation.mapped == nullptr || size == 0U || offset > allocation.size ||
        size > allocation.size - offset) {
        return false;
    }
    VkDeviceSize memorySize = 0;
    for (const Block& block : m_blocks) {
        if (block.id == allocation.blockId) {
            memorySize = block.size;
            break;
        }
    }
    if (memorySize == 0U) {
        for (const Dedicated& dedicated : m_dedicated) {
            if (dedicated.id == allocation.blockId) {
                memorySize = dedicated.size;
                break;
            }
        }
    }
    if (memorySize == 0U) {
        return false;
    }

    const VkDeviceSize begin = allocation.offset + offset;
    const VkDeviceSize end = begin + size;
    const VkDeviceSize alignedBegin = (begin / m_nonCoherentAtomSize) * m_nonCoherentAtomSize;
    VkDeviceSize alignedEnd = 0;
    if (!alignUp(end, m_nonCoherentAtomSize, alignedEnd) || alignedEnd > memorySize) {
        alignedEnd = memorySize;
    }

    range = VkMappedMemoryRange{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = allocation.memory;
    range.offset = alignedBegin;
    range.size = alignedEnd - alignedBegin;
    return true;
}

bool Allocator::flush(const Allocation& allocation, VkDeviceSize offset,
                      VkDeviceSize size) noexcept {
    if (allocation.coherent || size == 0U) {
        return true;
    }
    VkMappedMemoryRange range{};
    if (!hostRange(allocation, offset, size, range)) {
        return false;
    }
    return vkFlushMappedMemoryRanges(m_device, 1, &range) == VK_SUCCESS;
}

bool Allocator::invalidate(const Allocation& allocation, VkDeviceSize offset,
                           VkDeviceSize size) noexcept {
    if (allocation.coherent || size == 0U) {
        return true;
    }
    VkMappedMemoryRange range{};
    if (!hostRange(allocation, offset, size, range)) {
        return false;
    }
    return vkInvalidateMappedMemoryRanges(m_device, 1, &range) == VK_SUCCESS;
}

const AllocatorStats& Allocator::stats() const noexcept {
    return m_stats;
}

}
