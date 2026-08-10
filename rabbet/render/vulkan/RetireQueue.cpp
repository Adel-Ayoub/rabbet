#include "rabbet/render/vulkan/RetireQueue.h"

#include <algorithm>

namespace rb::vulkan {

namespace {

template <typename Entries>
void erasePending(Entries& entries, std::uint64_t completedFrameValue) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [completedFrameValue](const auto& entry) {
                                     return entry.frameValue <= completedFrameValue;
                                 }),
                  entries.end());
}

}

void RetireQueue::beginFrame(std::uint64_t frameValue) noexcept {
    m_currentFrame = frameValue;
}

void RetireQueue::retire(std::unique_ptr<Buffer> buffer) {
    if (buffer != nullptr) {
        m_buffers.push_back({m_currentFrame, std::move(buffer)});
    }
}

void RetireQueue::retire(std::unique_ptr<Image> image) {
    if (image != nullptr) {
        m_images.push_back({m_currentFrame, std::move(image)});
    }
}

void RetireQueue::retire(std::unique_ptr<Sampler> sampler) {
    if (sampler != nullptr) {
        m_samplers.push_back({m_currentFrame, std::move(sampler)});
    }
}

void RetireQueue::collect(std::uint64_t completedFrameValue) {
    erasePending(m_buffers, completedFrameValue);
    erasePending(m_images, completedFrameValue);
    erasePending(m_samplers, completedFrameValue);
}

void RetireQueue::drain() {
    m_buffers.clear();
    m_images.clear();
    m_samplers.clear();
}

std::size_t RetireQueue::pendingCount() const noexcept {
    return m_buffers.size() + m_images.size() + m_samplers.size();
}

std::uint64_t RetireQueue::currentFrame() const noexcept {
    return m_currentFrame;
}

}
