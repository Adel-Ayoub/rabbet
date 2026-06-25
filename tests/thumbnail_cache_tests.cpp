#include "rabbet/assets/ThumbnailCache.h"
#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

namespace {

constexpr rb::Uuid kA{0u, 1u};
constexpr rb::Uuid kB{0u, 2u};

static void freshCacheNeedsRender() {
    rb::ThumbnailCache cache;
    CHECK(cache.needsRender(kA, 0u));
    CHECK(!cache.contains(kA));
    CHECK(cache.size() == 0u);
}

static void markRenderedSatisfiesSameRevision() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 3u);
    CHECK(!cache.needsRender(kA, 3u));
    CHECK(cache.contains(kA));
    CHECK(cache.size() == 1u);
}

static void revisionBumpForcesRerender() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 1u);
    CHECK(cache.needsRender(kA, 2u)); // hot-reload bumped the revision
    cache.markRendered(kA, 2u);
    CHECK(!cache.needsRender(kA, 2u));
    CHECK(cache.size() == 1u); // re-render replaces, does not grow
}

static void invalidateForcesRerender() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 5u);
    cache.invalidate(kA);
    CHECK(cache.needsRender(kA, 5u));
    CHECK(!cache.contains(kA));
    CHECK(cache.size() == 0u);
}

static void clearEmptiesEverything() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 1u);
    cache.markRendered(kB, 1u);
    CHECK(cache.size() == 2u);
    cache.clear();
    CHECK(cache.size() == 0u);
    CHECK(cache.needsRender(kA, 1u));
    CHECK(cache.needsRender(kB, 1u));
}

static void uuidsAreIndependent() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 2u);
    CHECK(!cache.needsRender(kA, 2u));
    CHECK(cache.needsRender(kB, 2u)); // B untouched
}

} // namespace

int main() {
    freshCacheNeedsRender();
    markRenderedSatisfiesSameRevision();
    revisionBumpForcesRerender();
    invalidateForcesRerender();
    clearEmptiesEverything();
    uuidsAreIndependent();
    return rbtest::summary("thumbnail_cache");
}
