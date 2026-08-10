#include "rabbet/render/vulkan/Descriptors.h"

#include "rabbet/render/vulkan/Device.h"

#include <cstdio>

namespace rb::vulkan {

std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::create(
    const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings) {
    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device.handle(), &createInfo, nullptr, &layout) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan descriptor set layout creation failed\n");
        return nullptr;
    }
    return std::unique_ptr<DescriptorSetLayout>(new DescriptorSetLayout(device.handle(), layout));
}

DescriptorSetLayout::DescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout) noexcept
    : m_device(device), m_layout(layout) {}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    }
}

VkDescriptorSetLayout DescriptorSetLayout::handle() const noexcept {
    return m_layout;
}

std::unique_ptr<DescriptorPool> DescriptorPool::create(
    const Device& device, std::uint32_t maxSets, std::span<const VkDescriptorPoolSize> sizes) {
    if (maxSets == 0U || sizes.empty()) {
        std::fprintf(stderr, "Vulkan descriptor pool creation received an empty description\n");
        return nullptr;
    }

    VkDescriptorPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.maxSets = maxSets;
    createInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    createInfo.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device.handle(), &createInfo, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan descriptor pool creation failed\n");
        return nullptr;
    }
    return std::unique_ptr<DescriptorPool>(new DescriptorPool(device.handle(), pool));
}

DescriptorPool::DescriptorPool(VkDevice device, VkDescriptorPool pool) noexcept
    : m_device(device), m_pool(pool) {}

DescriptorPool::~DescriptorPool() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    }
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocateInfo, &set) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan descriptor set allocation failed\n");
        return VK_NULL_HANDLE;
    }
    return set;
}

bool DescriptorPool::reset() noexcept {
    if (vkResetDescriptorPool(m_device, m_pool, 0) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan descriptor pool reset failed\n");
        return false;
    }
    return true;
}

VkDescriptorPool DescriptorPool::handle() const noexcept {
    return m_pool;
}

}
