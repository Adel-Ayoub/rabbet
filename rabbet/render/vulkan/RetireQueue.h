#pragma once

#include "rabbet/render/vulkan/Buffer.h"
#include "rabbet/render/vulkan/Image.h"
#include "rabbet/render/vulkan/Sampler.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rb::vulkan {

// The allocator behind retired objects must outlive this queue.
class RetireQueue {
public:
    void beginFrame(std::uint64_t frameValue) noexcept;
    void retire(std::unique_ptr<Buffer> buffer);
    void retire(std::unique_ptr<Image> image);
    void retire(std::unique_ptr<Sampler> sampler);

    // collect destroys entries whose frame value the caller has seen fenced.
    void collect(std::uint64_t completedFrameValue);
    void drain();

    [[nodiscard]] std::size_t pendingCount() const noexcept;
    [[nodiscard]] std::uint64_t currentFrame() const noexcept;

private:
    template <typename Resource>
    struct Entry {
        std::uint64_t frameValue{0};
        std::unique_ptr<Resource> resource;
    };

    std::uint64_t m_currentFrame{0};
    std::vector<Entry<Buffer>> m_buffers;
    std::vector<Entry<Image>> m_images;
    std::vector<Entry<Sampler>> m_samplers;
};

}
