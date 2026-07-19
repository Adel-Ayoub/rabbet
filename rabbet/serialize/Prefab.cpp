#include "rabbet/serialize/Prefab.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/ParentRef.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

// The subtree-records shape. The loader also reads the older single-entity shape.
constexpr int kPrefabFormatVersion = 2;
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

// The record the rebuild treats as the root: the first without a parent ref. A malformed file
// where every record claims a parent falls back to the first record.
std::size_t rootRecordIndex(const nlohmann::json& entities) {
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].is_object() && entities[i].find("parent") == entities[i].end()) {
            return i;
        }
    }
    return 0;
}

// Links the created entities per the records' parent refs through the scene loader's shared
// tolerance (linkParentRef). The root record's ref (if a malformed file gave it one) is
// ignored so the root never re-parents itself.
void linkRecordParents(Scene& scene, const nlohmann::json& entities,
                       const std::vector<Entity>& created, std::size_t rootIndex) {
    for (std::size_t i = 0; i < created.size(); ++i) {
        if (i == rootIndex) {
            continue;
        }
        linkParentRef(scene, entities[i], created, i, "prefab", "record");
    }
}

} // namespace

nlohmann::json entityToPrefabJson(Scene& scene, const ComponentRegistry& registry, Entity source) {
    nlohmann::json doc;
    doc["version"] = kPrefabFormatVersion;
    nlohmann::json entities = nlohmann::json::array();
    if (scene.alive(source)) {
        // Collect the subtree breadth-first (cycle-tolerant), then map members to their
        // record positions so a child record can point at its parent by local id (the
        // source is always record 0).
        const std::vector<Entity> members = collectSubtree(scene, source);
        std::unordered_map<Entity, std::uint32_t> localIds;
        localIds.reserve(members.size());
        for (std::size_t i = 0; i < members.size(); ++i) {
            localIds.emplace(members[i], static_cast<std::uint32_t>(i));
        }
        for (std::size_t i = 0; i < members.size(); ++i) {
            nlohmann::json record;
            record["id"] = static_cast<std::uint32_t>(i);
            if (i > 0) {
                // A non-root member's parent sits inside the subtree by construction; the
                // root's own scene parent is deliberately not captured.
                const auto parentId = localIds.find(parentOf(scene, members[i]));
                if (parentId != localIds.end()) {
                    record["parent"] = parentId->second;
                }
            }
            nlohmann::json components = nlohmann::json::object();
            for (const ComponentRegistry::Entry& entry : registry.entries()) {
                if (entry.name == kPrefabInstanceComponent) {
                    continue; // a prefab never bakes in an instance link
                }
                if (entry.has(scene, members[i])) {
                    entry.save(scene, members[i], components[entry.name]);
                }
            }
            record["components"] = std::move(components);
            entities.push_back(std::move(record));
        }
    }
    doc["entities"] = std::move(entities);
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
            if (const auto entitiesIt = doc.find("entities");
                entitiesIt != doc.end() && entitiesIt->is_array()) {
                prefab.entities = *entitiesIt;
            } else if (const auto componentsIt = doc.find("components");
                       componentsIt != doc.end() && componentsIt->is_object()) {
                // The older single-entity shape normalizes to a one-record subtree.
                nlohmann::json record;
                record["id"] = 0;
                record["components"] = *componentsIt;
                prefab.entities.push_back(std::move(record));
            }
            prefab.path = p;
            return prefab;
        });
}

Entity instantiatePrefab(Scene& scene, const ComponentRegistry& registry,
                         const PrefabAsset& prefab) {
    if (!prefab.entities.is_array() || prefab.entities.empty()) {
        return scene.create(); // an empty or malformed prefab still yields a bare entity
    }
    std::vector<Entity> created;
    created.reserve(prefab.entities.size());
    for (std::size_t i = 0; i < prefab.entities.size(); ++i) {
        created.push_back(scene.create());
    }
    for (std::size_t i = 0; i < created.size(); ++i) {
        if (const auto componentsIt = prefab.entities[i].find("components");
            componentsIt != prefab.entities[i].end()) {
            loadComponentsInto(scene, registry, created[i], *componentsIt);
        }
    }
    const std::size_t rootIndex = rootRecordIndex(prefab.entities);
    linkRecordParents(scene, prefab.entities, created, rootIndex);
    return created[rootIndex];
}

void applyPrefab(Scene& scene, const ComponentRegistry& registry, Entity target,
                 const PrefabAsset& prefab) {
    if (!scene.alive(target)) {
        return;
    }
    // An empty or unreadable asset (a truncated file parses to no records) must not eat
    // the instance: bail before anything destructive, since revert is a repair gesture.
    if (!prefab.entities.is_array() || prefab.entities.empty()) {
        return;
    }
    // The instance is its whole subtree: entities parented beneath the root since
    // instantiation go with the stale children before the asset's tree is rebuilt.
    for (const Entity child : childrenOf(scene, target)) {
        destroySubtree(scene, child);
    }
    // Strip the root's own components (keeping the prefab link) so reverting clears overrides
    // and components added after instantiation. The root's place in the scene hierarchy also
    // survives, the Parent link being structural rather than registered.
    for (const ComponentRegistry::Entry& entry : registry.entries()) {
        if (entry.name == kPrefabInstanceComponent) {
            continue;
        }
        if (entry.has(scene, target)) {
            entry.remove(scene, target);
        }
    }
    const std::size_t rootIndex = rootRecordIndex(prefab.entities);
    std::vector<Entity> created(prefab.entities.size());
    for (std::size_t i = 0; i < created.size(); ++i) {
        created[i] = i == rootIndex ? target : scene.create();
    }
    for (std::size_t i = 0; i < created.size(); ++i) {
        if (const auto componentsIt = prefab.entities[i].find("components");
            componentsIt != prefab.entities[i].end()) {
            loadComponentsInto(scene, registry, created[i], *componentsIt);
        }
    }
    linkRecordParents(scene, prefab.entities, created, rootIndex);
}

std::string sanitizePrefabName(std::string name) {
    for (char& c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = std::isalnum(u) != 0 || c == '-' || c == '_' || c == '.' || c == ' ';
        if (!ok) {
            c = '_';
        }
    }
    const auto keep = [](char c) { return c != ' ' && c != '.'; };
    name.erase(name.begin(), std::find_if(name.begin(), name.end(), keep));
    name.erase(std::find_if(name.rbegin(), name.rend(), keep).base(), name.end());
    if (name.empty()) {
        name = "Prefab";
    }
    return name;
}

std::filesystem::path prefabFilePath(const std::filesystem::path& dir, const std::string& name) {
    const std::string base = sanitizePrefabName(name);
    std::error_code ec;
    std::filesystem::path candidate = dir / (base + ".prefab.json");
    for (int i = 1; i < 1000 && std::filesystem::exists(candidate, ec); ++i) {
        candidate = dir / (base + "_" + std::to_string(i) + ".prefab.json");
    }
    return candidate;
}

} // namespace rb
