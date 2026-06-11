#pragma once

#include <optional>

#include "rabbet/core/System.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/gl/DepthMap.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/PickBuffer.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/render/gl/Texture.h"

namespace rb {

class RenderSystem final : public System {
public:
    void onStart(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;

    // Renders entity indices to an offscreen integer buffer and reads back the entity
    // under (x, y) — viewport pixel coordinates with y measured from the top. Returns
    // an invalid entity when the pixel is empty. Drawn on demand (e.g. on a click).
    [[nodiscard]] Entity pick(Runtime& runtime, int x, int y);

private:
    [[nodiscard]] gl::Mesh* primitiveMesh(PrimitiveShape shape) noexcept;

    std::optional<gl::Shader> m_phong;
    std::optional<gl::Shader> m_pbr;
    std::optional<gl::Shader> m_depth;
    std::optional<gl::Shader> m_pick;
    std::optional<gl::Shader> m_flat; // unlit single-colour, for collider wireframes
    std::optional<gl::DepthMap> m_shadowMap;
    std::optional<gl::PickBuffer> m_pickBuffer;
    std::optional<gl::Mesh> m_missingMesh;
    std::optional<gl::Texture> m_missingTexture;
    std::optional<gl::Mesh> m_primitiveCube;
    std::optional<gl::Mesh> m_primitiveSphere;
    std::optional<gl::Mesh> m_primitivePlane;
    std::optional<gl::Texture> m_whiteTexture;
};

} // namespace rb
