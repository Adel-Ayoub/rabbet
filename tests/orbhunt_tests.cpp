#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/terrain/TerrainRenderData.h"
#include "rabbet/terrain/TerrainSystem.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr float kStep = 1.0f / 60.0f;

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

// World-space height of the terrain surface under (x, z): the nearest mesh vertex plus
// the island entity's transform offset. Good to a grid cell, which is all the checks need.
float surfaceHeight(const rb::TerrainRenderData& terrain, const glm::vec3& islandPos, float x,
                    float z) {
    const rb::MeshData* mesh = terrain.draws.front().mesh;
    float best = 1.0e9f;
    float height = 0.0f;
    for (const rb::Vertex& vertex : mesh->vertices) {
        const float dx = vertex.position.x - (x - islandPos.x);
        const float dz = vertex.position.z - (z - islandPos.z);
        const float d = dx * dx + dz * dz;
        if (d < best) {
            best = d;
            height = vertex.position.y;
        }
    }
    return islandPos.y + height;
}

// The shipped Orb Hunt content plays end to end headlessly: the scene loads from data, the
// island terrain is solid ground the player ball falls onto and rests on, every orb sits
// within reach of a ball on the surface, each collection spawns the pickup flash prefab
// (which later cleans itself up), and clearing the last orb spawns the victory prefab.
// No C++ content anywhere: everything comes from the committed scene + prefabs + Lua.
void orbHuntPlaysEndToEnd() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();
    const std::filesystem::path root = RB_FORGE_ASSETS;
    CHECK(database.scan(root, &assets) >= 34u);

    CHECK(rb::SceneSerializer::loadFromFile(runtime.scene(), registry,
                                            root / "orbhunt" / "orbhunt.scene.json"));

    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    runtime.addSystem<rb::TerrainSystem>();
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(&registry);
    runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    runtime.start();

    runtime.tick(0.1f);
    runtime.tick(0.1f); // the terrain debounce settles -> mesh built + published

    const rb::TerrainRenderData* terrain = runtime.tryResource<rb::TerrainRenderData>();
    CHECK(terrain != nullptr);
    CHECK(terrain != nullptr && terrain->draws.size() == 1u);
    if (terrain == nullptr || terrain->draws.empty()) {
        return;
    }

    const rb::Entity player = firstNamed(runtime.scene(), "Player");
    const rb::Entity island = firstNamed(runtime.scene(), "Island");
    CHECK(runtime.scene().alive(player));
    CHECK(runtime.scene().alive(island));
    CHECK(runtime.scene().alive(firstNamed(runtime.scene(), "GameCamera")));
    const glm::vec3 islandPos = runtime.scene().get<rb::Transform>(island).position;

    std::vector<rb::Entity> orbs = entitiesNamed(runtime.scene(), "Orb");
    CHECK(orbs.size() == 8u);

    // Every orb floats within reach of a ball rolling on the surface below it (this pins
    // the authored orb heights to the heightmap; edit the scene if the island changes).
    for (const rb::Entity orb : orbs) {
        const glm::vec3 p = runtime.scene().get<rb::Transform>(orb).position;
        const float ground = surfaceHeight(*terrain, islandPos, p.x, p.z);
        CHECK(p.y > ground + 0.2f);
        CHECK(p.y < ground + 1.7f);
    }

    runtime.beginPlay();
    runtime.setPlaying(true);
    for (int i = 0; i < 300; ++i) {
        runtime.tick(kStep); // the ball falls from its spawn and settles on the island
    }
    const glm::vec3 rest = runtime.scene().get<rb::Transform>(player).position;
    CHECK(rest.y > islandPos.y); // did not fall through the island
    CHECK(rest.y < 16.0f);       // and did fall from the spawn height

    // Collect every orb by teleporting it onto the live ball (orbs have no physics body,
    // so a transform write from outside the simulation sticks). Bounded so a broken
    // collection script fails the test instead of hanging it.
    bool flashSeen = false;
    for (int guard = 0; guard < 32; ++guard) {
        orbs = entitiesNamed(runtime.scene(), "Orb");
        if (orbs.empty()) {
            break;
        }
        const glm::vec3 ball = runtime.scene().get<rb::Transform>(player).position;
        runtime.scene().get<rb::Transform>(orbs.front()).position = ball;
        runtime.tick(kStep);
        flashSeen = flashSeen || !entitiesNamed(runtime.scene(), "PickupFlash").empty();
    }
    CHECK(entitiesNamed(runtime.scene(), "Orb").empty()); // every orb collected
    CHECK(flashSeen); // collections spawned the pickup flash prefab

    runtime.tick(kStep); // the controller notices the last orb is gone
    CHECK(runtime.scene().alive(firstNamed(runtime.scene(), "Victory")));

    for (int i = 0; i < 240; ++i) {
        runtime.tick(kStep); // 4 s: the flashes self-destruct via their script
    }
    CHECK(entitiesNamed(runtime.scene(), "PickupFlash").empty());
    CHECK(runtime.scene().alive(firstNamed(runtime.scene(), "Victory"))); // victory stays

    runtime.setPlaying(false);
    runtime.endPlay();
}

} // namespace

int main() {
    orbHuntPlaysEndToEnd();
    return rbtest::summary("orbhunt");
}
