#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "rabbet/core/Uuid.h"

namespace rb {

// Tracks which asset previews have already been rendered, keyed by uuid + the source revision they
// were built from, so a hot-reload (revision bump) or an explicit invalidate forces a re-render
// while an unchanged asset renders once. GL-free: the editor owns the actual preview textures and
// asks this only whether a (re-)render is due. Headless-testable.
class ThumbnailCache {
public:
    [[nodiscard]] bool needsRender(const Uuid& id, std::uint32_t revision) const noexcept {
        const auto it = m_built.find(id);
        return it == m_built.end() || it->second != revision;
    }

    void markRendered(const Uuid& id, std::uint32_t revision) { m_built[id] = revision; }

    [[nodiscard]] bool contains(const Uuid& id) const noexcept { return m_built.count(id) != 0; }

    void invalidate(const Uuid& id) { m_built.erase(id); }

    void clear() noexcept { m_built.clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return m_built.size(); }

private:
    std::unordered_map<Uuid, std::uint32_t> m_built;
};

} // namespace rb
