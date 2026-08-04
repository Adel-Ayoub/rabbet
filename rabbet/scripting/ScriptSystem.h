#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "rabbet/core/System.h"
#include "rabbet/scripting/ScriptField.h"

namespace rb {

class ComponentRegistry;

// Compiles `source` in a throwaway VM and reconciles `fields` with the script's declared
// `fields` table: matching overrides are kept, newly declared fields added, removed ones
// dropped. The editor calls this when a script is assigned so its tunables appear in the
// inspector before Play. A parse error leaves `fields` untouched.
void introspectScriptFields(const std::string& source, std::vector<ScriptField>& fields);

// Runs Lua behavior in Play mode. Owns one Lua VM; each scripted entity gets an isolated
// environment compiled from its .lua source. on_start runs on the first tick of a play
// session (so it survives Pause, which merely stops ticking) and on_update every tick
// after; both are bound to the entity's Transform, the Input resource, and frame time.
// A `world` table looks up other entities by Name (the same live-resolving Entity handle
// as `self`, answered through a per-tick name index), queues entity destruction and prefab
// spawning, and queues a scene switch (load_scene), all applied at end-of-tick so the
// component pool is never mutated while scripts iterate. sol2 and Lua live entirely behind
// the impl, so this header, and the rest of the engine and editor, never includes them.
// The third hook, on_late_update, is driven by ScriptLateSystem after the physics step:
// a script that tracks a body (a follow camera) reads the pose the step just wrote there,
// where on_update still reads last tick's. world.* calls made from the late hook follow
// the same deferral rules and are applied when the late pass ends.
class ScriptSystem final : public System {
public:
    // The registry powers world.spawn and world.load_scene (component loading through the
    // registry hooks). With no registry every other binding still works and both are
    // warned no-ops.
    explicit ScriptSystem(const ComponentRegistry* registry = nullptr);
    ~ScriptSystem() override;

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;
    ScriptSystem(ScriptSystem&&) = delete;
    ScriptSystem& operator=(ScriptSystem&&) = delete;

    void onUpdate(Runtime& runtime, float dt) override;
    void onPlayBegin(Runtime& runtime) override;
    void onPlayEnd(Runtime& runtime) override;

    // The tick's late half: on_late_update over the instances the early pass compiled.
    // ScriptLateSystem calls this; hosts never do directly.
    void lateUpdate(Runtime& runtime, float dt);

    // Live compiled-instance count. An observation seam: instances are reaped when their
    // entity dies, and leaks here are invisible from gameplay-facing state.
    [[nodiscard]] std::size_t instanceCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Dispatches on_late_update. Register it after PhysicsSystem in the host's Play set. It
// holds a plain pointer to the paired ScriptSystem and touches it only inside onUpdate,
// while both systems are alive inside the same runtime.
class ScriptLateSystem final : public System {
public:
    explicit ScriptLateSystem(ScriptSystem& scripts) : m_scripts(&scripts) {}

    void onUpdate(Runtime& runtime, float dt) override;

private:
    ScriptSystem* m_scripts;
};

} // namespace rb
