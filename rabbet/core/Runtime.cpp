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
    for (SystemEntry& entry : m_systems) {
        entry.system->onStart(*this);
    }
}

void Runtime::tick(float dt) {
    for (SystemEntry& entry : m_systems) {
        if (entry.phase == SystemPhase::Always || m_playing) {
            entry.system->onUpdate(*this, dt);
        }
    }
}

void Runtime::stop() {
    if (!m_started) {
        return;
    }
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        it->system->onStop(*this);
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
