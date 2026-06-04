#include "rabbet/serialize/ComponentRegistry.h"

namespace rb {

const ComponentRegistry::Entry* ComponentRegistry::find(std::string_view name) const noexcept {
    for (const Entry& entry : m_entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

void ComponentRegistry::setDrawer(std::string_view name, DrawFn fn) noexcept {
    for (Entry& entry : m_entries) {
        if (entry.name == name) {
            entry.drawInspector = fn;
            return;
        }
    }
}

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry registry;
    return registry;
}

} // namespace rb
