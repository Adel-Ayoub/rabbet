#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "tests/Test.h"

#include <string_view>
#include <vector>

namespace {

struct Marker {
    int loaded = 0;
};

class BaseModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "base"; }
    void configure(rb::Runtime& rt) override {
        if (!rt.hasResource<Marker>()) {
            rt.addResource<Marker>();
        }
        rt.resource<Marker>().loaded += 1;
    }
};

class DependentModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "dependent"; }
    [[nodiscard]] std::vector<std::string_view> dependencies() const override { return {"base"}; }
    void configure(rb::Runtime& rt) override { rt.resource<Marker>().loaded += 10; }
};

} // namespace

static void loadsWhenDependencySatisfied() {
    rb::Runtime rt;
    rt.loadModule<BaseModule>();
    rt.loadModule<DependentModule>();
    CHECK(rt.isModuleLoaded("base"));
    CHECK(rt.isModuleLoaded("dependent"));
    CHECK(rt.resource<Marker>().loaded == 11);
}

static void throwsWhenDependencyMissing() {
    rb::Runtime rt;
    bool threw = false;
    try {
        rt.loadModule<DependentModule>();
    } catch (const rb::ModuleError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!rt.isModuleLoaded("dependent"));
}

static void duplicateLoadIsIdempotent() {
    rb::Runtime rt;
    rt.loadModule<BaseModule>();
    rt.loadModule<BaseModule>();
    CHECK(rt.resource<Marker>().loaded == 1);
}

int main() {
    loadsWhenDependencySatisfied();
    throwsWhenDependencyMissing();
    duplicateLoadIsIdempotent();
    return rbtest::summary("core_module");
}
