#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

} // namespace

static void duplicateCopiesComponents() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity src = scene.create();
    scene.add<rb::Name>(src, rb::Name{"box"});
    rb::Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    scene.add<rb::Transform>(src, t);
    scene.add<rb::Primitive>(
        src, rb::Primitive{rb::PrimitiveShape::Sphere, {0.2f, 0.4f, 0.6f}, 0.5f, 0.3f});

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);

    CHECK(copy != src);
    CHECK(scene.alive(copy));
    CHECK(scene.aliveCount() == 2u);
    CHECK(scene.has<rb::Name>(copy));
    CHECK(scene.has<rb::Transform>(copy));
    CHECK(scene.has<rb::Primitive>(copy));

    CHECK(scene.get<rb::Name>(copy).value == "box");
    const rb::Transform& ct = scene.get<rb::Transform>(copy);
    CHECK(ct.position.x == 1.0f);
    CHECK(ct.position.z == 3.0f);
    CHECK(ct.scale.x == 2.0f);

    const rb::Primitive& cp = scene.get<rb::Primitive>(copy);
    CHECK(cp.shape == rb::PrimitiveShape::Sphere);
    CHECK(cp.color.y == 0.4f);
    CHECK(cp.metallic == 0.5f);
    CHECK(cp.roughness == 0.3f);
}

static void duplicateIsIndependent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity src = scene.create();
    rb::Transform t;
    t.position = {5.0f, 0.0f, 0.0f};
    scene.add<rb::Transform>(src, t);

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);
    scene.get<rb::Transform>(copy).position.x = 99.0f;

    CHECK(scene.get<rb::Transform>(src).position.x == 5.0f);
    CHECK(scene.get<rb::Transform>(copy).position.x == 99.0f);
}

static void duplicateResetsModelHandle() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity src = scene.create();
    rb::ModelRenderer renderer;
    renderer.model = rb::Uuid::generate();
    renderer.handle = rb::AssetHandle<rb::ModelAsset>{7u, 3u};
    CHECK(renderer.handle.valid());
    scene.add<rb::ModelRenderer>(src, renderer);

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);
    const rb::ModelRenderer& cr = scene.get<rb::ModelRenderer>(copy);
    CHECK(cr.model == renderer.model); // stable uuid copied
    CHECK(!cr.handle.valid());         // runtime handle reset for re-resolution
}

int main() {
    duplicateCopiesComponents();
    duplicateIsIndependent();
    duplicateResetsModelHandle();
    return rbtest::summary("serialize_duplicate");
}
