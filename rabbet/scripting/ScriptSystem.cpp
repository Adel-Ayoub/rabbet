#include "rabbet/scripting/ScriptSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <sol/sol.hpp>

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/PhysicsControl.h"
#include "rabbet/platform/Input.h"
#include "rabbet/scene/CameraShake.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/NameIndex.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/serialize/PrefabInstance.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

// A live handle to one entity, handed to its script as `self` and returned by world.find.
// It stores the Runtime and Entity (not a Transform*), resolving the component on each call
// so a sparse-set realloc between frames can never leave the script holding a dangling
// pointer; every method no-ops (or reports invalid) once the entity is destroyed.
struct ScriptEntity {
    Runtime* runtime = nullptr;
    Entity entity;

    Scene* scene() { return runtime != nullptr ? &runtime->scene() : nullptr; }

    Transform* transform() {
        Scene* s = scene();
        return s != nullptr ? s->tryGet<Transform>(entity) : nullptr;
    }

    bool valid() {
        Scene* s = scene();
        return s != nullptr && s->alive(entity);
    }

    double distanceTo(ScriptEntity& other) {
        Transform* a = transform();
        Transform* b = other.transform();
        if (a == nullptr || b == nullptr) {
            // Unresolvable (destroyed, or no Transform): infinitely far beats a fake 0,
            // which would read as "touching" to proximity checks.
            return std::numeric_limits<double>::infinity();
        }
        return static_cast<double>(glm::distance(a->position, b->position));
    }

    void translate(double dx, double dy, double dz) {
        if (Transform* t = transform()) {
            t->position +=
                glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
        }
    }

    void rotate(double dx, double dy, double dz) {
        if (Transform* t = transform()) {
            const glm::vec3 euler = glm::radians(
                glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)));
            t->rotation = glm::normalize(t->rotation * glm::quat(euler));
        }
    }

    std::tuple<double, double, double> position() {
        if (Transform* t = transform()) {
            return {static_cast<double>(t->position.x), static_cast<double>(t->position.y),
                    static_cast<double>(t->position.z)};
        }
        return {0.0, 0.0, 0.0};
    }

    void setPosition(double x, double y, double z) {
        if (Transform* t = transform()) {
            t->position =
                glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
    }

    std::tuple<double, double, double> getScale() {
        if (Transform* t = transform()) {
            return {static_cast<double>(t->scale.x), static_cast<double>(t->scale.y),
                    static_cast<double>(t->scale.z)};
        }
        return {1.0, 1.0, 1.0};
    }

    void setScale(double x, double y, double z) {
        if (Transform* t = transform()) {
            t->scale = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
    }

    std::string name() {
        if (Scene* s = scene()) {
            if (const Name* n = s->tryGet<Name>(entity)) {
                return n->value;
            }
        }
        return std::string{};
    }

    // Physics goes through the command/state resources, not the components: PhysicsSystem
    // drains the queue before it steps (same tick), and publishes velocities after (so a
    // read is one step stale). No-ops when no PhysicsSystem is in the session. Commands
    // move Dynamic bodies only: a Static body rejects the write, and a Kinematic one is
    // driven by its Transform (set_position), so a scripted velocity would only fight the
    // per-step kinematic sync. velocity() still reads back for both moving kinds.
    void pushPhysicsCommand(PhysicsCommands::Op op, double x, double y, double z) {
        if (runtime == nullptr) {
            return;
        }
        if (PhysicsCommands* commands = runtime->tryResource<PhysicsCommands>()) {
            commands->queue.push_back(
                {entity, op,
                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))});
        }
    }

    void setVelocity(double x, double y, double z) {
        pushPhysicsCommand(PhysicsCommands::Op::SetVelocity, x, y, z);
    }

    void impulse(double x, double y, double z) {
        pushPhysicsCommand(PhysicsCommands::Op::Impulse, x, y, z);
    }

    std::tuple<double, double, double> velocity() {
        if (runtime != nullptr) {
            if (const PhysicsState* state = runtime->tryResource<PhysicsState>()) {
                const auto it = state->linearVelocity.find(entity);
                if (it != state->linearVelocity.end()) {
                    return {static_cast<double>(it->second.x), static_cast<double>(it->second.y),
                            static_cast<double>(it->second.z)};
                }
            }
        }
        return {0.0, 0.0, 0.0};
    }
};

std::optional<Key> keyFromName(std::string_view name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') {
            return static_cast<Key>(static_cast<int>(Key::A) + (c - 'A'));
        }
        if (c >= 'a' && c <= 'z') {
            return static_cast<Key>(static_cast<int>(Key::A) + (c - 'a'));
        }
        if (c >= '0' && c <= '9') {
            return static_cast<Key>(static_cast<int>(Key::Num0) + (c - '0'));
        }
    }
    if (name == "Space") return Key::Space;
    if (name == "Escape") return Key::Escape;
    if (name == "Enter") return Key::Enter;
    if (name == "Tab") return Key::Tab;
    if (name == "Backspace") return Key::Backspace;
    if (name == "Left") return Key::Left;
    if (name == "Right") return Key::Right;
    if (name == "Up") return Key::Up;
    if (name == "Down") return Key::Down;
    if (name == "LeftShift") return Key::LeftShift;
    if (name == "LeftControl") return Key::LeftControl;
    if (name == "LeftAlt") return Key::LeftAlt;
    return std::nullopt;
}

// Reconcile `fields` with a script's declared `fields` table: keep an existing override
// when its name and type still match, add newly declared fields, drop removed ones.
void mergeDeclaredFields(std::vector<ScriptField>& fields, const sol::object& declared) {
    if (!declared.is<sol::table>()) {
        fields.clear();
        return;
    }
    std::vector<ScriptField> merged;
    sol::table table = declared.as<sol::table>();
    table.for_each([&](const sol::object& key, const sol::object& value) {
        if (key.get_type() != sol::type::string) {
            return;
        }
        ScriptField field;
        field.name = key.as<std::string>();
        switch (value.get_type()) {
        case sol::type::boolean:
            field.type = ScriptField::Type::Boolean;
            field.boolean = value.as<bool>();
            break;
        case sol::type::number:
            field.type = ScriptField::Type::Number;
            field.number = value.as<double>();
            break;
        case sol::type::string:
            field.type = ScriptField::Type::String;
            field.text = value.as<std::string>();
            break;
        default:
            return; // unsupported field type, skip it
        }
        for (const ScriptField& existing : fields) {
            if (existing.name == field.name && existing.type == field.type) {
                field = existing;
                break;
            }
        }
        merged.push_back(std::move(field));
    });
    fields = std::move(merged);
}

} // namespace

struct ScriptSystem::Impl {
    struct Instance {
        sol::environment env;
        sol::protected_function onStart;
        sol::protected_function onUpdate;
        sol::protected_function onLateUpdate;
        sol::object self;
        Uuid script;
        std::uint32_t revision = 0;
        bool compiled = false; // a compile was attempted for (script, revision)
        bool ok = false;       // the attempt produced runnable hooks
        bool started = false;  // on_start has run this play session
        // One throttle per hook, reset on recompile: an early-hook error must not
        // swallow the first report from the late hook, or the other way round.
        bool errorLoggedStart = false;
        bool errorLoggedUpdate = false;
        bool errorLoggedLate = false;
    };

    struct PendingSpawn {
        std::string prefab;
        glm::vec3 position{0.0f};
    };

    sol::state lua;
    std::unordered_map<Entity, Instance> instances;
    Input* input = nullptr;
    Runtime* runtime = nullptr;          // the tick currently running (null outside update)
    const ComponentRegistry* registry = nullptr; // enables world.spawn + load_scene; optional
    std::vector<Entity> pendingDestroy;  // world.destroy is applied after the script loop
    std::vector<PendingSpawn> pendingSpawn; // world.spawn likewise
    std::unordered_set<std::string> warnedSpawns; // one warning per failing prefab name
    NameIndex nameIndex;
    bool nameIndexRebuilt = false; // at most one rebuild per tick, on the tick's first find
    std::optional<std::string> pendingLoadScene; // world.load_scene; the tick's last call wins
    std::unordered_set<std::string> warnedLoads; // one warning per failing scene name

    Impl() {
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

        lua.new_usertype<ScriptEntity>(
            "Entity", sol::meta_function::construct, sol::no_constructor, //
            "translate", &ScriptEntity::translate, "rotate", &ScriptEntity::rotate, "position",
            &ScriptEntity::position, "set_position", &ScriptEntity::setPosition, "scale",
            &ScriptEntity::getScale, "set_scale", &ScriptEntity::setScale, "name",
            &ScriptEntity::name, "valid", &ScriptEntity::valid, "distance_to",
            &ScriptEntity::distanceTo, "set_velocity", &ScriptEntity::setVelocity, "velocity",
            &ScriptEntity::velocity, "impulse", &ScriptEntity::impulse);

        sol::table worldTable = lua.create_named_table("world");
        worldTable.set_function("find", [this](const std::string& name) -> sol::object {
            if (runtime == nullptr) {
                return sol::make_object(lua, sol::lua_nil);
            }
            // Rebuilt on the tick's first find, then served from the map: the Name pool
            // cannot change under the script loop (spawn/destroy/load_scene are deferred
            // and scripts cannot rename), so one rebuild answers every call this tick
            // with scan-identical, first-match results.
            if (!nameIndexRebuilt) {
                nameIndex.rebuild(runtime->scene());
                nameIndexRebuilt = true;
            }
            const Entity found = nameIndex.find(name);
            if (!found.valid()) {
                return sol::make_object(lua, sol::lua_nil);
            }
            return sol::make_object(lua, ScriptEntity{runtime, found});
        });
        // Queued, not immediate: scripts run inside the each<ScriptComponent> loop, and the
        // pool must never be mutated mid-iteration. Applied once the loop finishes, so a
        // destroyed entity still runs (and can be seen by) the rest of this tick's scripts.
        worldTable.set_function("destroy", [this](ScriptEntity& target) {
            pendingDestroy.push_back(target.entity);
        });
        // Deferred like destroy; the new entity exists from the next tick (find it by Name).
        worldTable.set_function("spawn",
                                [this](const std::string& prefab, double x, double y, double z) {
                                    pendingSpawn.push_back(
                                        {prefab, glm::vec3(static_cast<float>(x),
                                                           static_cast<float>(y),
                                                           static_cast<float>(z))});
                                });
        // Deferred to the end of the tick like spawn/destroy; a single pending slot, so
        // when two scripts request a switch in one tick the last request stands, like the
        // last write to a variable.
        worldTable.set_function("load_scene", [this](const std::string& name) {
            pendingLoadScene = name;
        });
        // Immediate, unlike spawn/destroy: a resource write mutates no pool, so it is
        // safe mid-loop. No-op without the resource, like the physics bridge.
        worldTable.set_function("shake", [this](double amplitude, double duration) {
            if (runtime == nullptr || !runtime->hasResource<CameraShake>()) {
                return;
            }
            // A NaN or inf here would poison the view matrix, for the whole session in
            // the inf-duration case. Clamp in double space; the float cast of an
            // out-of-range double is undefined.
            if (!std::isfinite(amplitude) || !std::isfinite(duration)) {
                return;
            }
            CameraShake& shake = runtime->resource<CameraShake>();
            shake.amplitude = static_cast<float>(std::clamp(amplitude, 0.0, 5.0));
            shake.duration = static_cast<float>(std::clamp(duration, 0.0, 10.0));
            shake.remaining = shake.duration;
        });

        sol::table inputTable = lua.create_named_table("input");
        inputTable.set_function("down", [this](const std::string& keyName) {
            const std::optional<Key> key = keyFromName(keyName);
            return input != nullptr && key.has_value() && input->keyDown(*key);
        });
        inputTable.set_function("pressed", [this](const std::string& keyName) {
            const std::optional<Key> key = keyFromName(keyName);
            return input != nullptr && key.has_value() && input->keyPressed(*key);
        });
    }

    void syncFields(Instance& instance, const ScriptComponent& component) {
        sol::object existing = instance.env["fields"];
        sol::table table;
        if (existing.is<sol::table>()) {
            table = existing.as<sol::table>();
        } else if (!component.fields.empty()) {
            table = lua.create_table();
            instance.env["fields"] = table;
        } else {
            return;
        }
        for (const ScriptField& field : component.fields) {
            switch (field.type) {
            case ScriptField::Type::Number:
                table[field.name] = field.number;
                break;
            case ScriptField::Type::Boolean:
                table[field.name] = field.boolean;
                break;
            case ScriptField::Type::String:
                table[field.name] = field.text;
                break;
            }
        }
    }

    void compile(Instance& instance, Runtime& rt, Entity entity, const ScriptAsset& asset,
                 ScriptComponent& component) {
        instance.ok = false;
        instance.started = false;
        instance.errorLoggedStart = false;
        instance.errorLoggedUpdate = false;
        instance.errorLoggedLate = false;
        const std::string chunkName = "@" + asset.path.string();

        sol::environment env(lua, sol::create, lua.globals());
        sol::load_result loaded = lua.load(asset.source, chunkName, sol::load_mode::text);
        if (!loaded.valid()) {
            const sol::error err = loaded;
            log::error("script: parse error in '{}': {}", asset.path.string(), err.what());
            return;
        }
        sol::protected_function chunk = loaded;
        sol::set_environment(env, chunk);
        const sol::protected_function_result ran = chunk();
        if (!ran.valid()) {
            const sol::error err = ran;
            log::error("script: load error in '{}': {}", asset.path.string(), err.what());
            return;
        }

        mergeDeclaredFields(component.fields, env["fields"]);
        instance.env = std::move(env);
        instance.self = sol::make_object(lua, ScriptEntity{&rt, entity});
        instance.onStart = instance.env["on_start"];
        instance.onUpdate = instance.env["on_update"];
        instance.onLateUpdate = instance.env["on_late_update"];
        instance.ok = true;
    }

    void invoke(Instance& instance, sol::protected_function& fn, const char* hook, float dt,
                bool& errorLogged) {
        if (!fn.valid()) {
            return;
        }
        sol::protected_function_result result =
            (dt < 0.0f) ? fn(instance.self) : fn(instance.self, static_cast<double>(dt));
        if (!result.valid() && !errorLogged) {
            const sol::error err = result;
            log::error("script: {} error: {}", hook, err.what());
            errorLogged = true; // throttle: report once until the script recompiles
        }
    }

    void update(Runtime& rt, float dt) {
        AssetManager* assets = rt.tryResource<AssetManager>();
        if (assets == nullptr) {
            return;
        }
        // Clamp real-world stalls like the particle and terrain steps do: a window drag
        // must not hand on_update a seconds-long dt that teleports script-driven motion
        // while physics (capped substeps) barely advances.
        constexpr float kMaxStep = 0.1f;
        dt = std::min(dt, kMaxStep);
        input = rt.tryResource<Input>();
        runtime = &rt;
        nameIndexRebuilt = false; // anything may have renamed or churned between ticks
        Scene& scene = rt.scene();

        scene.each<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
            if (!component.handle.valid()) {
                return;
            }
            ScriptAsset* asset = assets->get<ScriptAsset>(component.handle);
            if (asset == nullptr) {
                return;
            }
            Instance& instance = instances[entity];
            const bool needCompile = !instance.compiled || instance.script != component.script ||
                                     instance.revision != asset->revision;
            if (needCompile) {
                compile(instance, rt, entity, *asset, component);
                instance.script = component.script;
                instance.revision = asset->revision;
                instance.compiled = true;
            }
            if (!instance.ok) {
                return;
            }
            syncFields(instance, component);
            if (!instance.started) {
                invoke(instance, instance.onStart, "on_start", -1.0f, instance.errorLoggedStart);
                instance.started = true;
            }
            invoke(instance, instance.onUpdate, "on_update", dt, instance.errorLoggedUpdate);
        });

        // Apply the tick's world.spawn / world.destroy queues now that the component
        // iteration is over.
        applySpawns(rt);
        applyDestroys(scene);

        // Reap instances whose entity died outside the queue (editor delete during play,
        // a parent's cascade), mirroring the audio/particle reaps; the environments would
        // otherwise outlive their entities until Stop.
        for (auto it = instances.begin(); it != instances.end();) {
            if (scene.alive(it->first)) {
                ++it;
            } else {
                it = instances.erase(it);
            }
        }

        // The swap is the tick's last deferred apply: spawns and destroys queued alongside
        // it land in the outgoing scene and are replaced with it.
        applyLoadScene(rt);
    }

    // The tick's late half, driven by ScriptLateSystem after the physics step so a hook
    // here reads the poses the step just wrote. Compilation and on_start belong to the
    // early pass: an entity first scripted mid-tick waits for its first full tick. The
    // deferred queues drain again on the way out; they only hold what a late hook queued.
    void late(Runtime& rt, float dt) {
        AssetManager* assets = rt.tryResource<AssetManager>();
        if (assets == nullptr) {
            return;
        }
        constexpr float kMaxStep = 0.1f;
        dt = std::min(dt, kMaxStep);
        input = rt.tryResource<Input>();
        runtime = &rt;
        nameIndexRebuilt = false; // the early drain may have spawned or destroyed entities
        Scene& scene = rt.scene();

        scene.each<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
            // The same gate as the early pass: a slot cleared mid-play must silence this
            // hook too, or its leftover instance keeps steering the world with no script
            // on the component.
            if (!component.handle.valid() ||
                assets->get<ScriptAsset>(component.handle) == nullptr) {
                return;
            }
            auto it = instances.find(entity);
            if (it == instances.end() || !it->second.ok || !it->second.started) {
                return;
            }
            invoke(it->second, it->second.onLateUpdate, "on_late_update", dt,
                   it->second.errorLoggedLate);
        });

        applySpawns(rt);
        applyDestroys(scene);
        applyLoadScene(rt);
    }

    void applyDestroys(Scene& scene) {
        for (const Entity e : pendingDestroy) {
            // The whole subtree goes, mirroring the editor's delete: destroying a spawned
            // multi-entity prefab must not leak its children as orphans whose local pose
            // gets reinterpreted as a world pose.
            for (const Entity member : collectSubtree(scene, e)) {
                instances.erase(member);
                scene.destroy(member);
            }
            instances.erase(e); // a handle already dead when the queue drains still drops its instance
        }
        pendingDestroy.clear();
    }

    void warnSpawn(const std::string& prefab, const char* why) {
        if (warnedSpawns.insert(prefab).second) {
            log::warn("script: world.spawn('{}') skipped: {}", prefab, why);
        }
    }

    void warnLoad(const std::string& name, const char* why) {
        if (warnedLoads.insert(name).second) {
            log::warn("script: world.load_scene('{}') skipped: {}", name, why);
        }
    }

    // Replaces the live scene's contents in place. The editor's Play snapshot is untouched
    // (Stop still restores the scene that was playing when Play was pressed), and the other
    // Play systems reconcile against the swapped scene on their own next tick: physics
    // retires dead bodies and builds the new ones, audio reaps and re-voices, and this
    // system compiles the incoming scripts fresh so their on_start runs. loadFromFile
    // clears the scene only once the document proves to be a scene, so a missing, corrupt,
    // or shapeless file leaves the tick's scene exactly as it was.
    void applyLoadScene(Runtime& rt) {
        if (!pendingLoadScene.has_value()) {
            return;
        }
        const std::string request = std::move(*pendingLoadScene);
        pendingLoadScene.reset();
        AssetDatabase* database = rt.tryResource<AssetDatabase>();
        if (registry == nullptr || database == nullptr) {
            warnLoad(request, "scenes are not available in this session");
            return;
        }
        // Match the catalogued record name or the file stem, the world.spawn convention:
        // "title" and "title.scene" both address title.scene.json.
        const AssetDatabase::Record* record = nullptr;
        for (const AssetDatabase::Record& candidate : database->records()) {
            if (candidate.type != AssetType::Scene) {
                continue;
            }
            if (candidate.name == request ||
                candidate.path.stem().stem().string() == request) {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr) {
            warnLoad(request, "no catalogued scene has that name");
            return;
        }
        if (!SceneSerializer::loadFromFile(rt.scene(), *registry, record->path)) {
            warnLoad(request, "the scene failed to load");
            return;
        }
        // Every pre-swap entity is gone: drop their environments now so the incoming
        // scene's scripts compile fresh and start next tick. Nothing runs Lua after this
        // point in the tick, but the index must not read as authoritative for the new
        // scene, so re-arm the rebuild too.
        instances.clear();
        nameIndex.clear();
        nameIndexRebuilt = false;
        // The swap ends the old scene's kick; the incoming scene starts steady.
        if (rt.hasResource<CameraShake>()) {
            rt.resource<CameraShake>() = CameraShake{};
        }
    }

    void applySpawns(Runtime& rt) {
        if (pendingSpawn.empty()) {
            return;
        }
        AssetManager* assets = rt.tryResource<AssetManager>();
        AssetDatabase* database = rt.tryResource<AssetDatabase>();
        Scene& scene = rt.scene();
        for (const PendingSpawn& spawn : pendingSpawn) {
            if (registry == nullptr || assets == nullptr || database == nullptr) {
                warnSpawn(spawn.prefab, "prefabs are not available in this session");
                continue;
            }
            // Match the catalogued record name or the file stem, so both "orb" and
            // "orb.prefab" (the sidecar keeps the compound stem) address orb.prefab.json.
            const AssetDatabase::Record* record = nullptr;
            for (const AssetDatabase::Record& candidate : database->records()) {
                if (candidate.type != AssetType::Prefab) {
                    continue;
                }
                if (candidate.name == spawn.prefab ||
                    candidate.path.stem().stem().string() == spawn.prefab) {
                    record = &candidate;
                    break;
                }
            }
            if (record == nullptr) {
                warnSpawn(spawn.prefab, "no catalogued prefab has that name");
                continue;
            }
            AssetHandle<PrefabAsset> handle = assets->find<PrefabAsset>(record->id);
            if (!handle.valid()) {
                handle = loadPrefabAsset(*assets, record->path);
            }
            const PrefabAsset* prefab = assets->get<PrefabAsset>(handle);
            if (prefab == nullptr) {
                warnSpawn(spawn.prefab, "the prefab failed to load");
                continue;
            }
            const Entity e = instantiatePrefab(scene, *registry, *prefab);
            scene.add<PrefabInstance>(e, PrefabInstance{record->id, {}});
            if (Transform* transform = scene.tryGet<Transform>(e)) {
                transform->position = spawn.position;
            } else {
                Transform placed;
                placed.position = spawn.position;
                scene.add<Transform>(e, placed);
            }
        }
        pendingSpawn.clear();
    }
};

ScriptSystem::ScriptSystem(const ComponentRegistry* registry)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->registry = registry;
}
ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::onUpdate(Runtime& runtime, float dt) { m_impl->update(runtime, dt); }

void ScriptSystem::lateUpdate(Runtime& runtime, float dt) { m_impl->late(runtime, dt); }

void ScriptLateSystem::onUpdate(Runtime& runtime, float dt) {
    m_scripts->lateUpdate(runtime, dt);
}

std::size_t ScriptSystem::instanceCount() const { return m_impl->instances.size(); }

// A fresh play session starts every script from a clean state, so on_start runs again.
void ScriptSystem::onPlayBegin(Runtime&) {
    m_impl->instances.clear();
    m_impl->pendingDestroy.clear();
    m_impl->pendingSpawn.clear();
    m_impl->warnedSpawns.clear();
    m_impl->pendingLoadScene.reset();
    m_impl->warnedLoads.clear();
    m_impl->nameIndex.clear();
    m_impl->nameIndexRebuilt = false;
}
void ScriptSystem::onPlayEnd(Runtime&) {
    m_impl->instances.clear();
    m_impl->pendingDestroy.clear();
    m_impl->pendingSpawn.clear();
    m_impl->warnedSpawns.clear();
    m_impl->pendingLoadScene.reset();
    m_impl->warnedLoads.clear();
    m_impl->nameIndex.clear();
    m_impl->nameIndexRebuilt = false;
    m_impl->input = nullptr;
    m_impl->runtime = nullptr;
}

void introspectScriptFields(const std::string& source, std::vector<ScriptField>& fields) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    sol::environment env(lua, sol::create, lua.globals());
    sol::load_result loaded = lua.load(source, "@<introspect>", sol::load_mode::text);
    if (!loaded.valid()) {
        return; // a script that won't parse keeps whatever fields it already had
    }
    sol::protected_function chunk = loaded;
    sol::set_environment(env, chunk);
    if (!chunk().valid()) {
        return;
    }
    mergeDeclaredFields(fields, env["fields"]);
}

} // namespace rb
