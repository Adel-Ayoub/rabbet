#include "rabbet/core/Runtime.h"

namespace rb {

bool Runtime::isModuleLoaded(std::string_view name) const noexcept {
    for (const std::string& loaded : m_loadedModules) {
        if (loaded == name) {
            return true;
        }
    }
    return false;
}

void Runtime::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    m_running = true;
    for (std::unique_ptr<System>& system : m_systems) {
        system->onStart(*this);
    }
}

void Runtime::tick(float dt) {
    for (std::unique_ptr<System>& system : m_systems) {
        system->onUpdate(*this, dt);
    }
}

void Runtime::stop() {
    if (!m_started) {
        return;
    }
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        (*it)->onStop(*this);
    }
    m_started = false;
    m_running = false;
}

void Runtime::run() {
    start();
    while (m_running) {
        tick(m_fixedDelta);
    }
    stop();
}

} // namespace rb
