#include "rabbet/serialize/ParentRef.h"

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/util/Log.h"

namespace rb {

void linkParentRef(Scene& scene, const nlohmann::json& record,
                   const std::vector<Entity>& created, std::size_t index, const char* where,
                   const char* noun) {
    const auto parentIt = record.find("parent");
    if (parentIt == record.end()) {
        return;
    }
    if (!parentIt->is_number_unsigned() || parentIt->get<std::size_t>() >= created.size() ||
        parentIt->get<std::size_t>() == index) {
        log::warn("{}: {} {} has an invalid parent id, skipped", where, noun, index);
        return;
    }
    if (!setParent(scene, created[index], created[parentIt->get<std::size_t>()])) {
        log::warn("{}: {} {} parent id {} refused (cycle?)", where, noun, index,
                  parentIt->get<std::size_t>());
    }
}

} // namespace rb
