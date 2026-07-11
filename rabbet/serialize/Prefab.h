#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;
class ComponentRegistry;
class AssetManager;
struct PrefabAsset;

// Sanitizes a name into a safe prefab filename stem: keeps alphanumerics, space, '-', '_', '.';
// replaces anything else (path separators, etc.) with '_'; trims leading/trailing space and dots;
// falls back to "Prefab" when nothing usable remains. Pure.
[[nodiscard]] std::string sanitizePrefabName(std::string name);

// Builds a non-colliding "<dir>/<sanitized>.prefab.json" path, appending _1, _2, ... when a file
// already exists, so creating a prefab never silently overwrites an existing one.
[[nodiscard]] std::filesystem::path prefabFilePath(const std::filesystem::path& dir,
                                                   const std::string& name);

// Serializes the entity's subtree (itself plus every descendant) into a prefab document
// ({version, entities: [{id, parent?, components}]}), routing each entity through the registry
// save hooks. The root is record 0 and its own scene parent is not captured; the PrefabInstance
// link is excluded from every record so a prefab never bakes in an instance reference.
[[nodiscard]] nlohmann::json entityToPrefabJson(Scene& scene, const ComponentRegistry& registry,
                                                Entity source);

// Writes the entity's subtree as a .prefab.json file. Returns false on I/O failure or a dead
// source.
[[nodiscard]] bool savePrefabToFile(Scene& scene, const ComponentRegistry& registry, Entity source,
                                    const std::filesystem::path& path);

// Loads (or returns the cached) PrefabAsset for a .prefab.json file. A missing or unparseable
// file yields an invalid handle.
[[nodiscard]] AssetHandle<PrefabAsset> loadPrefabAsset(AssetManager& assets,
                                                       const std::filesystem::path& path);

// Recreates the prefab's entity subtree, remapping the records' parent refs onto the new
// entities, and returns the root. The caller attaches the PrefabInstance link to the root (it
// owns the prefab uuid); a malformed component or parent ref is skipped with a warning.
[[nodiscard]] Entity instantiatePrefab(Scene& scene, const ComponentRegistry& registry,
                                       const PrefabAsset& prefab);

// Reverts an instance to its prefab: destroys the instance's current descendants, strips the
// root's registered components (keeping the PrefabInstance link and the root's own place in the
// scene hierarchy), and rebuilds the subtree from the asset, discarding local overrides.
void applyPrefab(Scene& scene, const ComponentRegistry& registry, Entity target,
                 const PrefabAsset& prefab);

} // namespace rb
