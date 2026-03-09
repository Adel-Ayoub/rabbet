#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

#include <string>

static void generatesValidUnique() {
    const rb::Uuid a = rb::Uuid::generate();
    const rb::Uuid b = rb::Uuid::generate();
    CHECK(a.valid());
    CHECK(b.valid());
    CHECK(!(a == b));
    CHECK(!rb::Uuid{}.valid());
}

static void stringRoundTrips() {
    const rb::Uuid a = rb::Uuid::generate();
    const std::string text = a.toString();
    CHECK(text.size() == 32u);
    CHECK(rb::Uuid::fromString(text) == a);
    CHECK(!rb::Uuid::fromString("not-a-uuid").valid());
    CHECK(!rb::Uuid::fromString("").valid());
    CHECK(!rb::Uuid::fromString("dead").valid());
}

int main() {
    generatesValidUnique();
    stringRoundTrips();
    return rbtest::summary("assets_uuid");
}
