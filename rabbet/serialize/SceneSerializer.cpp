#include "rabbet/serialize/SceneSerializer.h"

#include <cstdint>
#include <fstream>
#include <unordered_map>

#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/ComponentRegistry.h"

namespace rb {

namespace {
constexpr int kSceneFormatVersion = 1;
}

nlohmann::json SceneSerializer::toJson(Scene& scene, const ComponentRegistry& registry) {
    nlohmann::json doc;
    doc["version"] = kSceneFormatVersion;
    nlohmann::json entities = nlohmann::json::array();

    // File ids are assigned sequentially in iteration order so a save -> load -> save
    // cycle is stable even when the live entity indices have gaps from destroys.
    std::uint32_t fileId = 0;
    for (const Entity e : scene.entities()) {
        nlohmann::json components = nlohmann::json::object();
        for (const ComponentRegistry::Entry& entry : registry.entries()) {
            if (entry.has(scene, e)) {
                entry.save(scene, e, components[entry.name]);
            }
        }
        nlohmann::json object;
        object["id"] = fileId++;
        object["components"] = std::move(components);
        entities.push_back(std::move(object));
    }

    doc["entities"] = std::move(entities);
    return doc;
}

void SceneSerializer::fromJson(const nlohmann::json& doc, Scene& scene,
                               const ComponentRegistry& registry) {
    const auto entitiesIt = doc.find("entities");
    if (entitiesIt == doc.end() || !entitiesIt->is_array()) {
        return;
    }

    // Two passes: create every entity first so a component can resolve references to
    // another entity through the id map (no built-in component needs this yet).
    std::unordered_map<std::uint32_t, Entity> idMap;
    idMap.reserve(entitiesIt->size());
    for (const nlohmann::json& object : *entitiesIt) {
        idMap.emplace(object.at("id").get<std::uint32_t>(), scene.create());
    }

    for (const nlohmann::json& object : *entitiesIt) {
        const Entity e = idMap.at(object.at("id").get<std::uint32_t>());
        const auto componentsIt = object.find("components");
        if (componentsIt == object.end()) {
            continue;
        }
        for (auto it = componentsIt->begin(); it != componentsIt->end(); ++it) {
            if (const ComponentRegistry::Entry* entry = registry.find(it.key())) {
                entry->load(scene, e, it.value());
            }
        }
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
        return false;
    }
    fromJson(doc, scene, registry);
    return true;
}

} // namespace rb
