#include "rabbet/serialize/Prefab.h"

#include <exception>
#include <fstream>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

constexpr int kPrefabFormatVersion = 1;
constexpr const char* kPrefabInstanceComponent = "PrefabInstance";

// Loads each component in the map onto the entity via the registry. The instance link is never
// instantiated (a prefab must not carry one). Unknown or malformed components are skipped.
void loadComponentsInto(Scene& scene, const ComponentRegistry& registry, Entity e,
                        const nlohmann::json& components) {
    if (!components.is_object()) {
        return;
    }
    for (auto it = components.begin(); it != components.end(); ++it) {
        if (it.key() == kPrefabInstanceComponent) {
            continue;
        }
        const ComponentRegistry::Entry* entry = registry.find(it.key());
        if (entry == nullptr) {
            log::warn("prefab: unknown component '{}' skipped", it.key());
            continue;
        }
        try {
            entry->load(scene, e, it.value());
        } catch (const std::exception& ex) {
            log::warn("prefab: component '{}' failed to load: {}", it.key(), ex.what());
        }
    }
}

} // namespace

nlohmann::json entityToPrefabJson(Scene& scene, const ComponentRegistry& registry, Entity source) {
    nlohmann::json doc;
    doc["version"] = kPrefabFormatVersion;
    nlohmann::json components = nlohmann::json::object();
    if (scene.alive(source)) {
        for (const ComponentRegistry::Entry& entry : registry.entries()) {
            if (entry.name == kPrefabInstanceComponent) {
                continue; // a prefab never bakes in an instance link
            }
            if (entry.has(scene, source)) {
                entry.save(scene, source, components[entry.name]);
            }
        }
    }
    doc["components"] = std::move(components);
    return doc;
}

bool savePrefabToFile(Scene& scene, const ComponentRegistry& registry, Entity source,
                      const std::filesystem::path& path) {
    if (!scene.alive(source)) {
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << entityToPrefabJson(scene, registry, source).dump(2) << '\n';
    return static_cast<bool>(out);
}

AssetHandle<PrefabAsset> loadPrefabAsset(AssetManager& assets, const std::filesystem::path& path) {
    return assets.load<PrefabAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<PrefabAsset> {
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
            if (doc.is_discarded()) {
                return std::nullopt;
            }
            PrefabAsset prefab;
            if (const auto it = doc.find("components"); it != doc.end() && it->is_object()) {
                prefab.components = *it;
            }
            prefab.path = p;
            return prefab;
        });
}

Entity instantiatePrefab(Scene& scene, const ComponentRegistry& registry,
                         const PrefabAsset& prefab) {
    const Entity e = scene.create();
    loadComponentsInto(scene, registry, e, prefab.components);
    return e;
}

void applyPrefab(Scene& scene, const ComponentRegistry& registry, Entity target,
                 const PrefabAsset& prefab) {
    if (!scene.alive(target)) {
        return;
    }
    // Strip the instance's own components (keeping the prefab link) so reverting clears overrides
    // and components added after instantiation, then re-load the prefab's data.
    for (const ComponentRegistry::Entry& entry : registry.entries()) {
        if (entry.name == kPrefabInstanceComponent) {
            continue;
        }
        if (entry.has(scene, target)) {
            entry.remove(scene, target);
        }
    }
    loadComponentsInto(scene, registry, target, prefab.components);
}

} // namespace rb
