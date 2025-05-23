#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "tests/Test.h"

#include <vector>

namespace {

struct Log {
    std::vector<int> events;
};

class Recorder final : public rb::System {
public:
    explicit Recorder(int id) : m_id(id) {}
    void onStart(rb::Runtime& rt) override { rt.resource<Log>().events.push_back(100 + m_id); }
    void onUpdate(rb::Runtime& rt, float) override { rt.resource<Log>().events.push_back(200 + m_id); }
    void onStop(rb::Runtime& rt) override { rt.resource<Log>().events.push_back(300 + m_id); }

private:
    int m_id;
};

class Stopper final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float) override {
        ++m_ticks;
        if (m_ticks >= 3) {
            rt.requestStop();
        }
    }
    [[nodiscard]] int ticks() const { return m_ticks; }

private:
    int m_ticks = 0;
};

} // namespace

static void lifecycleRunsInOrderThenReverse() {
    rb::Runtime rt;
    rt.addResource<Log>();
    rt.addSystem<Recorder>(1);
    rt.addSystem<Recorder>(2);

    rt.start();
    rt.tick(0.016f);
    rt.stop();

    const std::vector<int> expected{101, 102, 201, 202, 302, 301};
    CHECK(rt.resource<Log>().events == expected);
}

static void runLoopStopsOnRequest() {
    rb::Runtime rt;
    Stopper& stopper = rt.addSystem<Stopper>();
    rt.run();
    CHECK(stopper.ticks() == 3);
    CHECK(!rt.running());
}

static void sceneAndResourcesReachable() {
    rb::Runtime rt;
    CHECK(rt.scene().aliveCount() == 0u);
    const rb::Entity e = rt.scene().create();
    CHECK(rt.scene().alive(e));

    CHECK(!rt.hasResource<Log>());
    rt.addResource<Log>();
    CHECK(rt.hasResource<Log>());
}

int main() {
    lifecycleRunsInOrderThenReverse();
    runLoopStopsOnRequest();
    sceneAndResourcesReachable();
    return rbtest::summary("core_runtime");
}
