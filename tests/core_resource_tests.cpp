#include "rabbet/core/Resource.h"
#include "tests/Test.h"

namespace {

struct Counter {
    int value = 0;
};

struct Config {
    float scale = 1.0f;
};

} // namespace

static void addAndGet() {
    rb::ResourceRegistry reg;
    CHECK(!reg.has<Counter>());
    CHECK(reg.tryGet<Counter>() == nullptr);

    Counter& c = reg.add<Counter>();
    CHECK(reg.has<Counter>());
    c.value = 7;
    CHECK(reg.get<Counter>().value == 7);
    CHECK(reg.tryGet<Counter>() != nullptr);
}

static void distinctTypesAreIndependent() {
    rb::ResourceRegistry reg;
    reg.add<Counter>().value = 3;
    reg.add<Config>().scale = 2.0f;
    CHECK(reg.get<Counter>().value == 3);
    CHECK(reg.get<Config>().scale == 2.0f);
}

static void addReplacesExisting() {
    rb::ResourceRegistry reg;
    reg.add<Counter>().value = 1;
    reg.add<Counter>().value = 9;
    CHECK(reg.get<Counter>().value == 9);
}

int main() {
    addAndGet();
    distinctTypesAreIndependent();
    addReplacesExisting();
    return rbtest::summary("core_resource");
}
