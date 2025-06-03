#pragma once

#include <chrono>
#include <cstdint>

namespace rb {

class FrameClock {
public:
    float tick() noexcept {
        const TimePoint now = Clock::now();
        m_delta = std::chrono::duration<float>(now - m_last).count();
        m_last = now;
        ++m_frame;
        return m_delta;
    }

    [[nodiscard]] float delta() const noexcept { return m_delta; }
    [[nodiscard]] double elapsed() const noexcept {
        return std::chrono::duration<double>(Clock::now() - m_start).count();
    }
    [[nodiscard]] std::uint64_t frame() const noexcept { return m_frame; }

    void reset() noexcept {
        m_start = Clock::now();
        m_last = m_start;
        m_delta = 0.0f;
        m_frame = 0;
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_start = Clock::now();
    TimePoint m_last = Clock::now();
    float m_delta = 0.0f;
    std::uint64_t m_frame = 0;
};

} // namespace rb
