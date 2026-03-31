#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "rabbet/util/Log.h"

namespace rb::editor {

// A thread-safe ring of recent log records that backs the Console panel. install()
// routes the global rb::log sink into this buffer (also echoing to stderr). The
// owner must rb::log::resetSink() before destroying the buffer.
class LogBuffer {
public:
    struct Entry {
        rb::LogLevel level = rb::LogLevel::Info;
        std::string message;
    };

    void install();
    void push(rb::LogLevel level, std::string_view message);
    void clear();
    [[nodiscard]] std::vector<Entry> snapshot() const;
    [[nodiscard]] std::size_t size() const;

private:
    static constexpr std::size_t kMaxEntries = 5000;

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

} // namespace rb::editor
