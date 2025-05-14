#pragma once

namespace rb {

class Runtime;

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
