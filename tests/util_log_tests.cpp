#include "rabbet/util/Log.h"
#include "tests/Test.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

struct Captured {
    rb::LogLevel level;
    std::string text;
};

} // namespace

static void capturesAndFiltersByLevel() {
    std::vector<Captured> out;
    rb::log::setSink([&out](rb::LogLevel lvl, std::string_view msg) {
        out.push_back({lvl, std::string(msg)});
    });
    rb::log::setLevel(rb::LogLevel::Info);

    rb::log::debug("hidden {}", 1);
    rb::log::info("hello {}", 42);
    rb::log::error("boom {}", 7);

    CHECK(out.size() == 2u);
    CHECK(out[0].level == rb::LogLevel::Info);
    CHECK(out[0].text == "hello 42");
    CHECK(out[1].level == rb::LogLevel::Error);
    CHECK(out[1].text == "boom 7");

    rb::log::resetSink();
    rb::log::setLevel(rb::LogLevel::Info);
}

static void offSuppressesEverything() {
    std::vector<Captured> out;
    rb::log::setSink([&out](rb::LogLevel lvl, std::string_view msg) {
        out.push_back({lvl, std::string(msg)});
    });
    rb::log::setLevel(rb::LogLevel::Off);

    rb::log::error("nope");

    CHECK(out.empty());

    rb::log::resetSink();
    rb::log::setLevel(rb::LogLevel::Info);
}

int main() {
    capturesAndFiltersByLevel();
    offSuppressesEverything();
    return rbtest::summary("util_log");
}
