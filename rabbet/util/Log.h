#pragma once

#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace rb {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Off };

namespace log {

using Sink = std::function<void(LogLevel, std::string_view)>;

void setLevel(LogLevel level) noexcept;
[[nodiscard]] LogLevel level() noexcept;
void setSink(Sink sink);
void resetSink();

namespace detail {
[[nodiscard]] bool enabled(LogLevel level) noexcept;
void emit(LogLevel level, std::string_view message);
} // namespace detail

template <typename... Args>
void message(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    if (!detail::enabled(level)) {
        return;
    }
    detail::emit(level, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    message(LogLevel::Trace, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    message(LogLevel::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    message(LogLevel::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    message(LogLevel::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    message(LogLevel::Error, fmt, std::forward<Args>(args)...);
}

} // namespace log
} // namespace rb
