#pragma once

#include <optional>
#include <unordered_map>

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/ThumbnailCache.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/render/gl/Framebuffer.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/render/gl/Texture.h"

namespace rb {
class AssetManager;
} // namespace rb

namespace rb::editor {

// Renders small offscreen previews for assets and caches them by uuid + source revision. Textures
// are shown directly from their own gl::Texture; models and materials are rendered once into a
// shared offscreen framebuffer with a dedicated minimal lit shader, read back into an owned texture,
// and reused until the source asset changes. Every GL state it touches (bound framebuffer, viewport,
// depth/blend/cull/scissor) is saved and restored, and it renders at most a small budget of new
// previews per frame so opening a large folder never stalls. All draws are upright when displayed
// with the viewport's V-flip (uv0 = {0,1}, uv1 = {1,0}).
class ThumbnailRenderer {
public:
    static constexpr int kSize = 128;

    // Compiles the preview shader + builds the preview meshes. Needs a current GL context.
    void init();

    // Resets the per-frame render budget. Call once at the start of each editor frame.
    void beginFrame(int budget = 6) noexcept { m_budget = budget; }

    // Returns a GL texture id to draw for `record` (0 when not previewable, not ready, or past this
    // frame's budget, in which case it is retried next frame). Renders on a cache miss.
    [[nodiscard]] unsigned int thumbnail(AssetManager& assets,
                                         const AssetDatabase::Record& record);

    // Drops a cached preview so it re-renders on next request (e.g. after a reimport).
    void invalidate(const Uuid& id);

private:
    unsigned int textureThumbnail(AssetManager& assets, const AssetDatabase::Record& record);
    unsigned int modelThumbnail(AssetManager& assets, const AssetDatabase::Record& record);
    unsigned int materialThumbnail(AssetManager& assets, const AssetDatabase::Record& record);

    // Runs `draw` into the offscreen framebuffer with all touched GL state saved/restored, reads the
    // result back into the owned texture for `id`, and returns that texture's id.
    template <typename Draw>
    unsigned int renderInto(const Uuid& id, Draw&& draw);

    std::optional<gl::Framebuffer> m_fbo;
    std::optional<gl::Shader> m_shader;
    std::optional<gl::Mesh> m_sphere; // preview geometry for materials
    std::optional<gl::Texture> m_white;
    ThumbnailCache m_cache;
    std::unordered_map<Uuid, gl::Texture> m_textures; // owned model/material previews
    int m_budget = 0;
};

} // namespace rb::editor
