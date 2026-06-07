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
};

} // namespace rb
