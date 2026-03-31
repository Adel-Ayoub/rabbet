#include "editor/LogBuffer.h"

#include <cstdio>

namespace rb::editor {
namespace {

const char* tagOf(rb::LogLevel level) noexcept {
    switch (level) {
        case rb::LogLevel::Trace: return "[trace]";
        case rb::LogLevel::Debug: return "[debug]";
        case rb::LogLevel::Info: return "[info ]";
        case rb::LogLevel::Warn: return "[warn ]";
        case rb::LogLevel::Error: return "[error]";
        case rb::LogLevel::Off: return "[off  ]";
    }
    return "[?????]";
}

} // namespace

void LogBuffer::install() {
    rb::log::setSink([this](rb::LogLevel level, std::string_view message) {
        std::fprintf(stderr, "%s %.*s\n", tagOf(level), static_cast<int>(message.size()),
                     message.data());
        push(level, message);
    });
}

void LogBuffer::push(rb::LogLevel level, std::string_view message) {
    const std::lock_guard lock(m_mutex);
    if (m_entries.size() >= kMaxEntries) {
        // Drop the oldest fifth in one shift instead of one-per-push.
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + static_cast<std::ptrdiff_t>(kMaxEntries / 5));
    }
    m_entries.push_back(Entry{level, std::string(message)});
}

void LogBuffer::clear() {
    const std::lock_guard lock(m_mutex);
    m_entries.clear();
}

std::vector<LogBuffer::Entry> LogBuffer::snapshot() const {
    const std::lock_guard lock(m_mutex);
    return m_entries;
}

std::size_t LogBuffer::size() const {
    const std::lock_guard lock(m_mutex);
    return m_entries.size();
}

} // namespace rb::editor
