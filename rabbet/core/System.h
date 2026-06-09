#pragma once

namespace rb {

class Runtime;

// When a system's onUpdate runs. Always systems tick every frame (rendering,
// transforms, asset resolution); Play systems tick only while the runtime is
// playing, so the editor can gate gameplay behind Play/Stop.
enum class SystemPhase { Always, Play };

class System {
public:
    System() = default;
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = delete;
    System& operator=(System&&) = delete;
    virtual ~System() = default;

    virtual void onStart(Runtime&) {}
    virtual void onUpdate(Runtime&, float) {}
    virtual void onStop(Runtime&) {}

    // Play-session edges, distinct from the per-frame play gate: onPlayBegin fires once
    // when Play is pressed and onPlayEnd once on Stop. Pause/Step only gate onUpdate, so
    // they do not fire these. Only Play-phase systems receive them.
    virtual void onPlayBegin(Runtime&) {}
    virtual void onPlayEnd(Runtime&) {}
};

} // namespace rb
