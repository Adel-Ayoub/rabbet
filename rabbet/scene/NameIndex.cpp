#include "rabbet/scene/NameIndex.h"

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Name.h"

namespace rb {

void NameIndex::rebuild(Scene& scene) {
    m_first.clear();
    scene.each<Name>([this](Entity e, Name& name) {
        // try_emplace keeps the earliest occupant, preserving the scan's first-match rule
        // when several entities share a name.
        m_first.try_emplace(name.value, e);
    });
}

void NameIndex::clear() { m_first.clear(); }

Entity NameIndex::find(std::string_view name) const {
    const auto it = m_first.find(name);
    return it != m_first.end() ? it->second : kNullEntity;
}

} // namespace rb
