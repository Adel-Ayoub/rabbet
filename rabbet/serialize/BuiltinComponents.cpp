#include "rabbet/serialize/BuiltinComponents.h"

#include "rabbet/serialize/ComponentRegistry.h"

namespace rb {

void registerBuiltinComponents(ComponentRegistry& registry) {
    registry.add<Name>("Name");
    registry.add<Transform>("Transform");
    registry.add<Camera>("Camera");
    registry.add<DirectionalLight>("DirectionalLight");
    registry.add<PointLight>("PointLight");
    registry.add<ModelRenderer>("ModelRenderer");
    registry.add<MaterialComponent>("MaterialComponent");
    registry.add<Primitive>("Primitive");
    registry.add<ScriptComponent>("ScriptComponent");
    registry.add<RigidBody>("RigidBody");
    registry.add<BoxCollider>("BoxCollider");
    registry.add<SphereCollider>("SphereCollider");
    registry.add<SoundEmitter>("SoundEmitter");
    registry.add<PrefabInstance>("PrefabInstance");
}

} // namespace rb
