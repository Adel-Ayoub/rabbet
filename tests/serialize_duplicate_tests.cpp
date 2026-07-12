#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Parent.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <vector>

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

static void duplicateSubtreeMirrorsTheTree() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity root = scene.create();
    scene.add<rb::Name>(root, rb::Name{"root"});
    const rb::Entity child = scene.create();
    scene.add<rb::Name>(child, rb::Name{"child"});
    const rb::Entity grand = scene.create();
    scene.add<rb::Name>(grand, rb::Name{"grand"});
    CHECK(rb::setParent(scene, child, root));
    CHECK(rb::setParent(scene, grand, child));

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, root);

    CHECK(scene.aliveCount() == 6u);
    CHECK(copy != root);
    CHECK(rb::parentOf(scene, copy) == rb::kNullEntity);

    const std::vector<rb::Entity> copiedChildren = rb::childrenOf(scene, copy);
    CHECK(copiedChildren.size() == 1u);
    if (!copiedChildren.empty()) {
        const rb::Entity copiedChild = copiedChildren.front();
        CHECK(copiedChild != child); // linked to the copy, not shared with the original
        CHECK(scene.get<rb::Name>(copiedChild).value == "child");
        const std::vector<rb::Entity> copiedGrand = rb::childrenOf(scene, copiedChild);
        CHECK(copiedGrand.size() == 1u);
        CHECK(!copiedGrand.empty() && scene.get<rb::Name>(copiedGrand.front()).value == "grand");
    }

    // The original tree is untouched.
    CHECK(rb::childrenOf(scene, root).size() == 1u);
    CHECK(rb::parentOf(scene, child) == root);
    CHECK(rb::parentOf(scene, grand) == child);
}

static void duplicateSubtreeKeepsTheRootParent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity base = scene.create();
    const rb::Entity source = scene.create();
    const rb::Entity leaf = scene.create();
    CHECK(rb::setParent(scene, source, base));
    CHECK(rb::setParent(scene, leaf, source));

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, source);

    CHECK(rb::parentOf(scene, copy) == base); // the copy stays a sibling of the source
    CHECK(rb::childrenOf(scene, base).size() == 2u);
    CHECK(rb::childrenOf(scene, copy).size() == 1u);
}

// A dead source duplicates to nothing: no zombie copy is minted for either entry point.
static void duplicateDeadSourceCreatesNothing() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity doomed = scene.create();
    scene.add<rb::Name>(doomed, rb::Name{"gone"});
    scene.destroy(doomed);
    const std::size_t before = scene.aliveCount();

    CHECK(!scene.alive(rb::SceneSerializer::duplicateEntity(scene, registry, doomed)));
    CHECK(scene.aliveCount() == before);
    CHECK(!scene.alive(rb::SceneSerializer::duplicateSubtree(scene, registry, doomed)));
    CHECK(scene.aliveCount() == before);
}

// Corrupt cyclic links (authored around the gate) duplicate each member once instead of
// spinning the collector forever.
static void duplicateSubtreeOfCorruptCycleTerminates() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity a = scene.create();
    scene.add<rb::Name>(a, rb::Name{"a"});
    const rb::Entity b = scene.create();
    scene.add<rb::Name>(b, rb::Name{"b"});
    scene.add<rb::Parent>(a, rb::Parent{b});
    scene.add<rb::Parent>(b, rb::Parent{a});

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, a);

    CHECK(scene.alive(copy));
    CHECK(scene.aliveCount() == 4u); // two originals + exactly two copies
}

int main() {
    duplicateCopiesComponents();
    duplicateIsIndependent();
    duplicateResetsModelHandle();
    duplicateSubtreeMirrorsTheTree();
    duplicateSubtreeKeepsTheRootParent();
    duplicateDeadSourceCreatesNothing();
    duplicateSubtreeOfCorruptCycleTerminates();
    return rbtest::summary("serialize_duplicate");
}
