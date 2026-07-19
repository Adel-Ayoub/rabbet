#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Resource.h"
#include "rabbet/core/System.h"
#include "rabbet/ecs/Scene.h"

namespace rb {

class Runtime {
public:
    Runtime() = default;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    ~Runtime() = default;

    [[nodiscard]] Scene& scene() noexcept { return m_scene; }
    [[nodiscard]] const Scene& scene() const noexcept { return m_scene; }
    [[nodiscard]] ResourceRegistry& resources() noexcept { return m_resources; }

    template <Resource T, typename... Args>
    T& addResource(Args&&... args) {
        return m_resources.add<T>(std::forward<Args>(args)...);
    }

    template <Resource T>
    [[nodiscard]] T& resource() noexcept {
        return m_resources.get<T>();
    }

    template <Resource T>
    [[nodiscard]] T* tryResource() noexcept {
        return m_resources.tryGet<T>();
    }

    template <Resource T>
    [[nodiscard]] bool hasResource() const noexcept {
        return m_resources.has<T>();
    }

    template <typename S, SystemPhase Phase = SystemPhase::Always, typename... Args>
    S& addSystem(Args&&... args) {
        static_assert(std::derived_from<S, System>, "S must derive from rb::System");
        auto system = std::make_unique<S>(std::forward<Args>(args)...);
        S& ref = *system;
        m_systems.push_back(SystemEntry{std::move(system), Phase});
        return ref;
    }

    template <typename M, typename... Args>
    void loadModule(Args&&... args) {
        static_assert(std::derived_from<M, Module>, "M must derive from rb::Module");
        M module{std::forward<Args>(args)...};
        const std::string moduleName{module.name()};
        if (isModuleLoaded(moduleName)) {
            return;
        }
        for (const std::string_view dependency : module.dependencies()) {
            if (!isModuleLoaded(dependency)) {
                throw ModuleError("module '" + moduleName + "' depends on '" +
                                  std::string(dependency) + "' which has not been loaded");
            }
        }
        module.configure(*this);
        m_loadedModules.emplace_back(moduleName);
    }

    [[nodiscard]] bool isModuleLoaded(std::string_view name) const noexcept;

    void start();
    void tick(float dt);
    void stop();
    void run();

    // Drive the play-session edges. The editor calls beginPlay() on Play and endPlay()
    // on Stop; each notifies Play-phase systems once (idempotent). Independent of the
    // per-frame setPlaying() gate, which Pause/Step toggle every frame.
    void beginPlay();
    void endPlay();
    [[nodiscard]] bool inPlaySession() const noexcept { return m_inPlaySession; }

    void requestStop() noexcept { m_running = false; }
    [[nodiscard]] bool running() const noexcept { return m_running; }
    void setFixedDelta(float seconds) noexcept { m_fixedDelta = seconds; }

    // Gates Play-phase systems. Edit mode (playing == false) ticks only Always
    // systems; Play mode ticks both. The editor toggles this on Play/Stop.
    void setPlaying(bool playing) noexcept { m_playing = playing; }
    [[nodiscard]] bool playing() const noexcept { return m_playing; }

private:
    struct SystemEntry {
        std::unique_ptr<System> system;
        SystemPhase phase = SystemPhase::Always;
    };

    Scene m_scene;
    ResourceRegistry m_resources;
    std::vector<SystemEntry> m_systems;
    std::vector<std::string> m_loadedModules;
    float m_fixedDelta = kFixedDelta;
    bool m_running = false;
    bool m_started = false;
    bool m_playing = false;
    bool m_inPlaySession = false;
};

} // namespace rb
