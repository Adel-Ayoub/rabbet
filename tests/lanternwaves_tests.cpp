#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Clock.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleSystem.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/render/ModelLoader.h"
#include "rabbet/render/SkyboxComponent.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/PrefabAssetResolveSystem.h"
#include "rabbet/serialize/PrefabInstance.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/terrain/TerrainRenderData.h"
#include "rabbet/terrain/TerrainSystem.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr float kStep = rb::kFixedDelta; // drive the demo at the engine's fixed cadence

std::vector<rb::Entity> entitiesNamed(rb::Scene& scene, const std::string& name) {
    std::vector<rb::Entity> found;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (n.value == name) {
            found.push_back(e);
        }
    });
    return found;
}

rb::Entity firstNamed(rb::Scene& scene, const std::string& name) {
    const std::vector<rb::Entity> found = entitiesNamed(scene, name);
    return found.empty() ? rb::Entity{} : found.front();
}

// Every check that follows reads a transform, so a missing entity has to fail loudly
// here rather than index a pool out of bounds once asserts are compiled out.
bool require(rb::Scene& scene, rb::Entity e) {
    CHECK(scene.alive(e));
    return scene.alive(e);
}

// The headless subset of the editor's play systems: everything the shipped content
// actually exercises without a GL context (no render, skybox, audio or material passes).
struct Demo {
    rb::ComponentRegistry registry;
    rb::Runtime runtime;
    std::filesystem::path root{RB_FORGE_ASSETS};

    explicit Demo(const char* sceneFile) {
        rb::registerBuiltinComponents(registry);
        rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
        rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();
        CHECK(database.scan(root, &assets) >= 55u);

        CHECK(rb::SceneSerializer::loadFromFile(runtime.scene(), registry,
                                                root / "lanternwaves" / sceneFile));

        runtime.addSystem<rb::ScriptAssetResolveSystem>();
        // Resolves PrefabInstance links, so a placed prefab's uuid has to name a real
        // catalogued asset instead of merely being well-formed hex.
        runtime.addSystem<rb::PrefabAssetResolveSystem>();
        runtime.addSystem<rb::TerrainSystem>();
        runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(&registry);
        runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
        runtime.addSystem<rb::TransformSystem>();
        runtime.addSystem<rb::ParticleSystem>(); // the content's emitters actually simulate
        runtime.start();

        runtime.tick(0.1f);
        runtime.tick(0.1f); // the terrain debounce settles -> mesh built + published
    }

    rb::Scene& scene() { return runtime.scene(); }

    // Mirrors how world.spawn and world.load_scene resolve a name: the catalogued record
    // name, or the double stem ("arena" addresses arena.scene.json).
    [[nodiscard]] bool assetResolves(const std::string& name, rb::AssetType type) {
        const rb::AssetDatabase* db = runtime.tryResource<rb::AssetDatabase>();
        if (db == nullptr) {
            return false;
        }
        for (const rb::AssetDatabase::Record& r : db->records()) {
            if (r.type == type && (r.name == name || r.path.stem().stem().string() == name)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool uuidResolves(const char* text) {
        const rb::AssetDatabase* db = runtime.tryResource<rb::AssetDatabase>();
        return db != nullptr && db->find(rb::Uuid::fromString(text)) != nullptr;
    }

    void play() {
        runtime.beginPlay();
        runtime.setPlaying(true);
    }

    void stop() {
        runtime.setPlaying(false);
        runtime.endPlay();
    }

    void tick(int count) {
        for (int i = 0; i < count; ++i) {
            runtime.tick(kStep);
        }
    }
};

// Every uuid the demo hands to the engine has to name a catalogued asset. A dangling
// script uuid is silently inert at runtime (the handle just stays dead), so the demo
// would ship with a ball that does not steer and no error anywhere.
void everyReferencedAssetResolves() {
    Demo demo("arena.scene.json");

    CHECK(demo.assetResolves("arena", rb::AssetType::Scene)); // world.load_scene targets
    CHECK(demo.assetResolves("title", rb::AssetType::Scene));
    CHECK(demo.assetResolves("wisp", rb::AssetType::Prefab)); // world.spawn targets
    CHECK(demo.assetResolves("wisp_pop", rb::AssetType::Prefab));
    CHECK(demo.assetResolves("flame_pop", rb::AssetType::Prefab));
    CHECK(demo.assetResolves("triumph", rb::AssetType::Prefab));

    CHECK(demo.uuidResolves("1a20000000000000000000000000e007")); // wisp.lua
    CHECK(demo.uuidResolves("1a20000000000000000000000000e008")); // waves.lua
    CHECK(demo.uuidResolves("1a20000000000000000000000000e006")); // title.lua
    CHECK(demo.uuidResolves("1a20000000000000000000000000e009")); // pop.lua
    CHECK(demo.uuidResolves("0f10000000000000000000000000d001")); // the lamp post prefab
    // Shared with the first demo: removing those files would break this one silently.
    CHECK(demo.uuidResolves("0eb0000000000000000000000000c001")); // player.lua
    CHECK(demo.uuidResolves("0eb0000000000000000000000000c006")); // the catch chime
    CHECK(demo.uuidResolves("0eb0000000000000000000000000c007")); // the win sting
    CHECK(demo.uuidResolves("2b30000000000000000000000000f005")); // the clearing's albedo
    CHECK(demo.uuidResolves("2b30000000000000000000000000f001")); // the boulder model
    CHECK(demo.uuidResolves("9f8e7d6c5b4a39281706f5e4d3c2b1a0")); // the particle sprite
}

// The scenes lean on committed third-party art, so the files have to be real geometry the
// importer can read, not just catalogued paths. loadModel is GL-free, so the mesh itself
// is checkable here rather than only once something renders.
void shippedPropArtLoads() {
    Demo demo("arena.scene.json");

    const std::optional<rb::Model> model =
        rb::loadModel(demo.root / "models" / "boulder_01" / "boulder_01_1k.gltf");
    CHECK(model.has_value());
    if (model.has_value()) {
        CHECK(!model->meshes.empty());
        CHECK(!model->meshes.empty() && model->meshes.front().data.vertices.size() > 1000u);
        CHECK(!model->meshes.empty() && !model->meshes.front().data.indices.empty());
        CHECK(!model->materials.empty()); // the gltf's own material, with its texture refs
    }

    // and the clearing is actually dressed with them
    CHECK(entitiesNamed(demo.scene(), "Boulder").size() >= 5u);

    // Both scenes carry their own night sky, and every face has to be a catalogued
    // texture: SkyboxSystem refuses to swap a half-resolved cubemap, so one bad uuid
    // silently leaves whatever sky was on screen before.
    for (const char* file : {"arena.scene.json", "title.scene.json"}) {
        Demo scene(file);
        const rb::SkyboxComponent* sky = rb::activeSkybox(scene.scene());
        CHECK(sky != nullptr);
        if (sky == nullptr) {
            continue;
        }
        for (const rb::Uuid& face : sky->faces) {
            CHECK(face.valid());
            CHECK(scene.uuidResolves(face.toString().c_str()));
        }
    }
}

// The arena is authored data end to end: a flat clearing with physics, the lamp post
// placed as a prefab instance whose link resolves, the flames that stand in for the
// lantern's health, and a ball that sits still until someone drives it.
void arenaContentIsWellFormed() {
    Demo demo("arena.scene.json");
    rb::Scene& scene = demo.scene();

    const rb::TerrainRenderData* terrain = demo.runtime.tryResource<rb::TerrainRenderData>();
    CHECK(terrain != nullptr);
    CHECK(terrain != nullptr && terrain->draws.size() == 1u);

    // A placed prefab instance: the link has to resolve to the real catalogued prefab,
    // not just carry well-formed hex (a bogus uuid keeps the authored child records but
    // leaves the handle dead).
    const rb::Entity lamp = firstNamed(scene, "Lamp Post");
    if (!require(scene, lamp)) {
        return;
    }
    const rb::PrefabInstance* link = scene.tryGet<rb::PrefabInstance>(lamp);
    CHECK(link != nullptr);
    CHECK(link != nullptr && link->handle.valid());

    const rb::Entity head = firstNamed(scene, "Head");
    const rb::Entity glow = firstNamed(scene, "Glow");
    CHECK(require(scene, head));
    CHECK(require(scene, glow));
    CHECK(rb::parentOf(scene, head) == lamp);
    CHECK(rb::parentOf(scene, firstNamed(scene, "Pole")) == lamp);
    CHECK(rb::parentOf(scene, glow) == head); // three deep, as captured

    CHECK(entitiesNamed(scene, "Flame").size() == 3u);
    CHECK(require(scene, firstNamed(scene, "WaveController")));
    CHECK(require(scene, firstNamed(scene, "GameCamera")));
    CHECK(entitiesNamed(scene, "Wisp").empty()); // wisps only ever arrive by world.spawn

    const rb::Entity player = firstNamed(scene, "Player");
    if (!require(scene, player)) {
        return;
    }
    demo.play();
    demo.tick(240); // 4 s on the clearing
    const glm::vec3 settled = scene.get<rb::Transform>(player).position;
    CHECK(settled.y > 0.35f); // did not sink through the terrain body
    CHECK(settled.y < 0.75f); // and is sitting on it, ball-radius high

    // An untouched ball has to stay put. A spawn drop turns into lateral velocity on the
    // flat clearing, which would send it coasting into the lantern with nobody driving.
    demo.tick(300); // 5 s more, still no input
    const glm::vec3 later = scene.get<rb::Transform>(player).position;
    const float drift = glm::length(glm::vec2(later.x - settled.x, later.z - settled.z));
    CHECK(drift < 0.25f);
    demo.stop();
}

// The wave controller spawns the wisp prefab through world.spawn, each arriving as a
// whole three-entity subtree, and each one closes on the lantern under its own script.
void wavesSpawnNestedWispsThatCloseIn() {
    Demo demo("arena.scene.json");
    rb::Scene& scene = demo.scene();
    const rb::Entity lampEntity = firstNamed(scene, "Lamp Post");
    if (!require(scene, lampEntity)) {
        return;
    }
    const glm::vec3 lamp = scene.get<rb::Transform>(lampEntity).position;
    demo.play();

    demo.tick(60); // inside the controller's lead-in
    CHECK(entitiesNamed(scene, "Wisp").empty());

    // Catch the first arrival on the tick it appears, so the ring radius below is the
    // authored spawn distance rather than however far it has already walked.
    rb::Entity wisp{};
    for (int i = 0; i < 300 && !scene.alive(wisp); ++i) {
        demo.tick(1);
        wisp = firstNamed(scene, "Wisp");
    }
    if (!require(scene, wisp)) {
        return;
    }

    // Spawned from the nested prefab, so the core and its light came along and the
    // hierarchy is rebuilt. Walk the links from this wisp rather than trusting name
    // lookups, which would pair a second wisp's core with this one's root.
    const std::vector<rb::Entity> children = rb::childrenOf(scene, wisp);
    CHECK(children.size() == 1u);
    if (children.size() != 1u) {
        return;
    }
    const rb::Entity core = children.front();
    CHECK(scene.get<rb::Name>(core).value == "Wisp Core");
    const std::vector<rb::Entity> grandchildren = rb::childrenOf(scene, core);
    CHECK(grandchildren.size() == 1u);
    CHECK(grandchildren.size() == 1u &&
          scene.get<rb::Name>(grandchildren.front()).value == "Wisp Light");
    CHECK(scene.tryGet<rb::PrefabInstance>(wisp) != nullptr);

    // It arrives out on the authored ring and walks itself in toward the lantern.
    const glm::vec3 start = scene.get<rb::Transform>(wisp).position;
    const float startFlat = glm::length(glm::vec2(start.x - lamp.x, start.z - lamp.z));
    CHECK(startFlat > 19.0f); // the controller's authored spawn radius is 20

    demo.tick(120); // 2 s of drift
    CHECK(scene.alive(wisp));
    const glm::vec3 now = scene.get<rb::Transform>(wisp).position;
    const float nowFlat = glm::length(glm::vec2(now.x - lamp.x, now.z - lamp.z));
    CHECK(nowFlat < startFlat - 3.0f);         // closed real ground
    CHECK(std::fabs(now.y - start.y) < 0.001f); // and held its lantern height
    demo.stop();
}

// Left alone, the wisps reach the lantern and put it out one flame at a time; the
// controller then hands the session back to the title scene.
void unansweredWispsDrainTheLantern() {
    Demo demo("arena.scene.json");
    rb::Scene& scene = demo.scene();
    demo.play();

    const std::size_t flamesAtStart = entitiesNamed(scene, "Flame").size();
    CHECK(flamesAtStart == 3u);

    // Bounded: a wisp that never arrives fails the check below instead of hanging here.
    bool drained = false;
    for (int i = 0; i < 1800 && !drained; ++i) {
        demo.tick(1);
        drained = entitiesNamed(scene, "Flame").size() < flamesAtStart;
    }
    CHECK(drained); // an arriving wisp took a flame

    bool lampOut = false;
    for (int i = 0; i < 2400 && !lampOut; ++i) {
        demo.tick(1);
        lampOut = entitiesNamed(scene, "Flame").empty();
    }
    CHECK(lampOut); // the wisps put the lantern out

    // The controller settles, then world.load_scene swaps the arena for the title.
    bool backAtTitle = false;
    for (int i = 0; i < 600 && !backAtTitle; ++i) {
        demo.tick(1);
        backAtTitle = scene.alive(firstNamed(scene, "TitleCamera"));
    }
    CHECK(backAtTitle);
    CHECK(entitiesNamed(scene, "Wisp").empty()); // the arena went with it
    demo.stop();
}

// Catching every wisp keeps the lantern lit through all three waves, which is the win:
// the controller spawns the triumph prefab and then returns to the title.
void catchingEveryWispSavesTheLantern() {
    Demo demo("arena.scene.json");
    rb::Scene& scene = demo.scene();
    demo.play();
    demo.tick(240); // the ball settles on the clearing first

    bool popSeen = false;
    bool won = false;

    // Wisps carry no physics body, so a transform write from outside the simulation
    // sticks: drop each one onto the live ball to "catch" it, the way the orb hunt suite
    // collects orbs. The player handle is re-read every pass because a scene swap would
    // retire it. Bounded so a stalled controller fails rather than hangs.
    for (int i = 0; i < 3600 && !won; ++i) {
        const rb::Entity player = firstNamed(scene, "Player");
        const std::vector<rb::Entity> wisps = entitiesNamed(scene, "Wisp");
        if (!wisps.empty() && scene.alive(player)) {
            scene.get<rb::Transform>(wisps.front()).position =
                scene.get<rb::Transform>(player).position;
        }
        demo.tick(1);
        popSeen = popSeen || !entitiesNamed(scene, "WispPop").empty();
        won = scene.alive(firstNamed(scene, "Triumph"));
    }

    CHECK(popSeen); // catches spawned the burst prefab
    CHECK(won);     // every wave survived -> the lantern held
    // Not merely "some flame left": catching everything means nothing ever reached it.
    CHECK(entitiesNamed(scene, "Flame").size() == 3u);

    bool backAtTitle = false;
    for (int i = 0; i < 600 && !backAtTitle; ++i) {
        demo.tick(1);
        backAtTitle = scene.alive(firstNamed(scene, "TitleCamera"));
    }
    CHECK(backAtTitle); // and the win returns to the title too
    demo.stop();
}

// A caught wisp bursts, and the burst clears itself up. Checked mid-session, well before
// either ending: after a swap the scene is empty of everything, so a burst that never
// expired would look identical to one that did.
void caughtWispsBurstAndCleanUp() {
    Demo demo("arena.scene.json");
    rb::Scene& scene = demo.scene();
    demo.play();
    demo.tick(240); // the ball settles first

    rb::Entity wisp{};
    for (int i = 0; i < 300 && !scene.alive(wisp); ++i) {
        demo.tick(1);
        wisp = firstNamed(scene, "Wisp");
    }
    const rb::Entity player = firstNamed(scene, "Player");
    if (!require(scene, wisp) || !require(scene, player)) {
        return;
    }

    scene.get<rb::Transform>(wisp).position = scene.get<rb::Transform>(player).position;
    demo.tick(2); // the wisp notices the ball, destroys itself and queues the burst
    CHECK(!scene.alive(wisp));
    CHECK(!entitiesNamed(scene, "WispPop").empty()); // the burst prefab spawned

    // Now leave it alone. The burst's own script has to retire it; nothing else will,
    // and the session is still mid-arena so no scene swap can mask a leak.
    demo.tick(180); // 3 s, past the burst's authored lifetime
    CHECK(entitiesNamed(scene, "WispPop").empty());
    CHECK(!entitiesNamed(scene, "Flame").empty()); // still mid-game, not an ending
    demo.stop();
}

// The title holds for its authored attract delay and then drops into the arena on its
// own. That covers the entry transition headlessly, where no input device exists to
// press anything, and it only lands inside the window if the scene's field override
// (8 s) is really applied over the script's own default (20 s).
void titleAttractsIntoTheArena() {
    Demo demo("title.scene.json");
    rb::Scene& scene = demo.scene();

    const rb::Entity lamp = firstNamed(scene, "Lamp Post");
    if (!require(scene, lamp)) {
        return;
    }
    const rb::PrefabInstance* link = scene.tryGet<rb::PrefabInstance>(lamp);
    CHECK(link != nullptr);
    CHECK(link != nullptr && link->handle.valid());
    CHECK(rb::parentOf(scene, firstNamed(scene, "Head")) == lamp);
    CHECK(require(scene, firstNamed(scene, "Drift")));

    demo.play();
    demo.tick(360); // 6 s: still short of the attract delay
    CHECK(scene.alive(firstNamed(scene, "TitleCamera")));
    CHECK(entitiesNamed(scene, "WaveController").empty());

    bool inArena = false;
    for (int i = 0; i < 360 && !inArena; ++i) { // 6 s more covers the 8 s override
        demo.tick(1);
        inArena = scene.alive(firstNamed(scene, "WaveController"));
    }
    CHECK(inArena); // world.load_scene("arena") named a scene that really loads
    CHECK(require(scene, firstNamed(scene, "Player")));
    CHECK(entitiesNamed(scene, "TitleCamera").empty()); // replaced, not merged
    demo.stop();
}

} // namespace

int main() {
    everyReferencedAssetResolves();
    shippedPropArtLoads();
    arenaContentIsWellFormed();
    wavesSpawnNestedWispsThatCloseIn();
    unansweredWispsDrainTheLantern();
    caughtWispsBurstAndCleanUp();
    catchingEveryWispSavesTheLantern();
    titleAttractsIntoTheArena();
    return rbtest::summary("lanternwaves");
}
