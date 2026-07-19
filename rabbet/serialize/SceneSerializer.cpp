#include "rabbet/serialize/SceneSerializer.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/ParentRef.h"
#include "rabbet/util/Log.h"

namespace rb {

namespace {
constexpr int kSceneFormatVersion = 1;
}

nlohmann::json SceneSerializer::toJson(Scene& scene, const ComponentRegistry& registry) {
    nlohmann::json doc;
    doc["version"] = kSceneFormatVersion;
    nlohmann::json entities = nlohmann::json::array();

    // Sequential ids in iteration order: human-readable, and a save -> load -> save
    // cycle stays stable. Loading is positional, so ids need not round-trip verbatim.
    const std::vector<Entity> order = scene.entities();
    std::unordered_map<Entity, std::uint32_t> fileIds;
    fileIds.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        fileIds.emplace(order[i], static_cast<std::uint32_t>(i));
    }

    for (const Entity e : order) {
        nlohmann::json components = nlohmann::json::object();
        for (const ComponentRegistry::Entry& entry : registry.entries()) {
            if (entry.has(scene, e)) {
                entry.save(scene, e, components[entry.name]);
            }
        }
        nlohmann::json object;
        object["id"] = fileIds.at(e);
        // The hierarchy link is structural, not a component payload: records refer to
        // each other by file id, remapped to the created entities on load. parentOf
        // already drops a stale link to a destroyed parent.
        const Entity parent = parentOf(scene, e);
        if (parent.valid()) {
            const auto parentId = fileIds.find(parent);
            if (parentId != fileIds.end()) {
                object["parent"] = parentId->second;
            }
        }
        object["components"] = std::move(components);
        entities.push_back(std::move(object));
    }

    doc["entities"] = std::move(entities);
    return doc;
}

Entity SceneSerializer::duplicateEntity(Scene& scene, const ComponentRegistry& registry,
                                        Entity source) {
    if (!scene.alive(source)) {
        return kNullEntity; // a dead source must not mint an empty copy
    }
    const Entity copy = scene.create();
    nlohmann::json data;
    for (const ComponentRegistry::Entry& entry : registry.entries()) {
        if (!entry.has(scene, source)) {
            continue;
        }
        entry.save(scene, source, data);
        try {
            entry.load(scene, copy, data);
        } catch (const std::exception& ex) {
            log::warn("duplicate: component '{}' failed to copy: {}", entry.name, ex.what());
        }
    }
    return copy;
}

Entity SceneSerializer::duplicateSubtree(Scene& scene, const ComponentRegistry& registry,
                                         Entity source) {
    // Collect before creating any copy, so the copies never show up as children mid-walk;
    // the collector is cycle-tolerant, so corrupt links duplicate each member once.
    const std::vector<Entity> originals = collectSubtree(scene, source);
    if (originals.empty()) {
        return kNullEntity;
    }
    std::unordered_map<Entity, Entity> copies;
    copies.reserve(originals.size());
    for (const Entity original : originals) {
        copies.emplace(original, duplicateEntity(scene, registry, original));
    }
    for (const Entity original : originals) {
        const Entity parent = parentOf(scene, original);
        if (!parent.valid()) {
            continue;
        }
        const auto inside = copies.find(parent);
        setParent(scene, copies.at(original),
                  inside != copies.end() ? inside->second : parent);
    }
    return copies.at(source);
}

void SceneSerializer::fromJson(const nlohmann::json& doc, Scene& scene,
                               const ComponentRegistry& registry) {
    const auto entitiesIt = doc.find("entities");
    if (entitiesIt == doc.end() || !entitiesIt->is_array()) {
        return;
    }

    // Create one entity per record first (positional), then load components. A
    // malformed record or component is skipped with a warning instead of throwing,
    // so a corrupt or out-of-date scene file degrades rather than crashing.
    const std::size_t count = entitiesIt->size();
    std::vector<Entity> created;
    created.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        created.push_back(scene.create());
    }

    for (std::size_t i = 0; i < count; ++i) {
        const nlohmann::json& object = (*entitiesIt)[i];
        const auto componentsIt = object.find("components");
        if (componentsIt == object.end() || !componentsIt->is_object()) {
            continue;
        }
        const Entity e = created[i];
        for (auto it = componentsIt->begin(); it != componentsIt->end(); ++it) {
            const ComponentRegistry::Entry* entry = registry.find(it.key());
            if (entry == nullptr) {
                log::warn("scene load: unknown component '{}' skipped", it.key());
                continue;
            }
            try {
                entry->load(scene, e, it.value());
            } catch (const std::exception& ex) {
                log::warn("scene load: component '{}' failed to load: {}", it.key(), ex.what());
            }
        }
    }

    // Parent links resolve only after every record has an entity, so a child record may
    // precede its parent in the file. linkParentRef turns a hand-authored bad ref or
    // cycle into a warn + skip instead of corrupt runtime state.
    for (std::size_t i = 0; i < count; ++i) {
        linkParentRef(scene, (*entitiesIt)[i], created, i, "scene load", "entity");
    }
}

bool SceneSerializer::saveToFile(Scene& scene, const ComponentRegistry& registry,
                                 const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << toJson(scene, registry).dump(2) << '\n';
    return static_cast<bool>(out);
}

bool SceneSerializer::loadFromFile(Scene& scene, const ComponentRegistry& registry,
                                   const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        log::warn("scene load: '{}' is not valid JSON", path.string());
        return false;
    }
    // Valid JSON is not enough to clear the live scene: a hand-edited "{}" would parse,
    // wipe everything, and load nothing. Only a document with an entity array counts.
    const auto entitiesIt = doc.find("entities");
    if (entitiesIt == doc.end() || !entitiesIt->is_array()) {
        log::warn("scene load: '{}' is not a scene document", path.string());
        return false;
    }
    // Loading a file replaces the scene; call fromJson directly to merge instead.
    scene.clear();
    fromJson(doc, scene, registry);
    return true;
}

} // namespace rb
