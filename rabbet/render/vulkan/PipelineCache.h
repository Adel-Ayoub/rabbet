#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

namespace rb::vulkan {

class Device;

enum class PipelineCacheLoad {
    fresh,
    loaded,
    rejected,
};

class PipelineCache {
public:
    static std::unique_ptr<PipelineCache> create(const Device& device, const std::string& path);

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    ~PipelineCache();

    [[nodiscard]] VkPipelineCache handle() const noexcept;
    [[nodiscard]] PipelineCacheLoad loadResult() const noexcept;
    [[nodiscard]] std::size_t loadedBytes() const noexcept;
    [[nodiscard]] bool save();
    [[nodiscard]] std::size_t savedBytes() const noexcept;

private:
    PipelineCache(VkDevice device, VkPipelineCache cache, std::string path,
                  PipelineCacheLoad loadResult, std::size_t loadedBytes) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkPipelineCache m_cache{VK_NULL_HANDLE};
    std::string m_path;
    PipelineCacheLoad m_loadResult{PipelineCacheLoad::fresh};
    std::size_t m_loadedBytes{0};
    std::size_t m_savedBytes{0};
};

}
