#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rabbet/core/System.h"
#include "rabbet/scripting/ScriptField.h"

namespace rb {

// Compiles `source` in a throwaway VM and reconciles `fields` with the script's declared
// `fields` table: matching overrides are kept, newly declared fields added, removed ones
// dropped. The editor calls this when a script is assigned so its tunables appear in the
// inspector before Play. A parse error leaves `fields` untouched.
void introspectScriptFields(const std::string& source, std::vector<ScriptField>& fields);

// Runs Lua behavior in Play mode. Owns one Lua VM; each scripted entity gets an isolated
// environment compiled from its .lua source. on_start runs on the first tick of a play
// session (so it survives Pause, which merely stops ticking) and on_update every tick
// after; both are bound to the entity's Transform, the Input resource, and frame time.
// sol2 and Lua live entirely behind the impl, so this header — and the rest of the
// engine and editor — never include them.
class ScriptSystem final : public System {
public:
    ScriptSystem();
    ~ScriptSystem() override;

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;
    ScriptSystem(ScriptSystem&&) = delete;
    ScriptSystem& operator=(ScriptSystem&&) = delete;

    void onUpdate(Runtime& runtime, float dt) override;
    void onPlayBegin(Runtime& runtime) override;
    void onPlayEnd(Runtime& runtime) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rb
