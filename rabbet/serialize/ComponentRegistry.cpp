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

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry registry;
    return registry;
}

} // namespace rb
