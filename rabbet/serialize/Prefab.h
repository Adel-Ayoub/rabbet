#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;
class ComponentRegistry;
class AssetManager;
struct PrefabAsset;

// Serializes one entity's registered components into a prefab document ({version, components}),
// routing each through the registry save hooks. The PrefabInstance link is excluded so a prefab
// never bakes in an instance reference (only serializable components are captured).
[[nodiscard]] nlohmann::json entityToPrefabJson(Scene& scene, const ComponentRegistry& registry,
                                                Entity source);

// Writes the entity as a .prefab.json file. Returns false on I/O failure or a dead source.
[[nodiscard]] bool savePrefabToFile(Scene& scene, const ComponentRegistry& registry, Entity source,
                                    const std::filesystem::path& path);

// Loads (or returns the cached) PrefabAsset for a .prefab.json file. A missing or unparseable
// file yields an invalid handle.
[[nodiscard]] AssetHandle<PrefabAsset> loadPrefabAsset(AssetManager& assets,
                                                       const std::filesystem::path& path);

// Creates a new entity carrying copies of the prefab's components and returns it. The caller
// attaches the PrefabInstance link (it owns the prefab uuid); a malformed component is skipped.
[[nodiscard]] Entity instantiatePrefab(Scene& scene, const ComponentRegistry& registry,
                                       const PrefabAsset& prefab);

// Reverts an entity to its prefab: removes its registered components (keeping the PrefabInstance
// link) and re-loads the prefab's component data, discarding local overrides.
void applyPrefab(Scene& scene, const ComponentRegistry& registry, Entity target,
                 const PrefabAsset& prefab);

} // namespace rb
