#include "rabbet/util/Log.h"

#include <cstdio>
#include <mutex>

namespace rb::log {
namespace {

LogLevel& levelStorage() noexcept {
    static LogLevel value = LogLevel::Info;
    return value;
}

std::mutex& sinkMutex() {
    static std::mutex mutex;
    return mutex;
}

const char* tagOf(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "[trace]";
        case LogLevel::Debug: return "[debug]";
        case LogLevel::Info:  return "[info ]";
        case LogLevel::Warn:  return "[warn ]";
        case LogLevel::Error: return "[error]";
        case LogLevel::Off:   return "[off  ]";
    }
    return "[?????]";
}

void defaultSink(LogLevel level, std::string_view message) {
    std::fprintf(stderr, "%s %.*s\n", tagOf(level), static_cast<int>(message.size()), message.data());
}

Sink& sinkStorage() {
    static Sink sink = defaultSink;
    return sink;
}

} // namespace

void setLevel(LogLevel level) noexcept {
    levelStorage() = level;
}

LogLevel level() noexcept {
    return levelStorage();
}

void setSink(Sink sink) {
    const std::lock_guard lock(sinkMutex());
    sinkStorage() = sink ? std::move(sink) : Sink(defaultSink);
}

void resetSink() {
    const std::lock_guard lock(sinkMutex());
    sinkStorage() = defaultSink;
}

namespace detail {

bool enabled(LogLevel level) noexcept {
    return level >= levelStorage();
}

void emit(LogLevel level, std::string_view message) {
    const std::lock_guard lock(sinkMutex());
    sinkStorage()(level, message);
}

} // namespace detail
} // namespace rb::log
