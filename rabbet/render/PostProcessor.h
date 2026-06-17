#pragma once

#include <optional>
#include <vector>

#include "rabbet/render/gl/ColorTarget.h"
#include "rabbet/render/gl/Shader.h"

namespace rb {

struct PostProcess;

// Owns the HDR -> LDR post-processing chain: a soft-knee bloom (a half-res down/upsample mip chain),
// a composite pass (exposure, tone-map, colour grade, gamma), and an optional FXAA pass. Every step
// is a full-screen-triangle pass through colour-only targets. process() takes the linear-HDR scene
// texture and returns the LDR texture to present; targets are lazily (re)built when the size changes.
class PostProcessor {
public:
    PostProcessor() = default;
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    PostProcessor(PostProcessor&&) = delete;
    PostProcessor& operator=(PostProcessor&&) = delete;
    ~PostProcessor();

    // Compiles the post shaders and creates the full-screen VAO. Requires a live GL context.
    void init();

    // Runs the chain on `hdrTexture` (an RGBA16F colour texture of size width x height) and returns
    // the id of the LDR texture holding the final image. Leaves framebuffer 0 bound.
    [[nodiscard]] unsigned int process(unsigned int hdrTexture, int width, int height,
                                       const PostProcess& settings);

private:
    void ensureTargets(int width, int height);
    void drawFullscreen() const noexcept;

    std::optional<gl::Shader> m_prefilter;
    std::optional<gl::Shader> m_downsample;
    std::optional<gl::Shader> m_upsample;
    std::optional<gl::Shader> m_composite;
    std::optional<gl::Shader> m_fxaa;

    std::vector<gl::ColorTarget> m_bloom; // mip chain, RGBA16F; [0] is half resolution
    std::optional<gl::ColorTarget> m_ldr; // composite output, RGBA8
    std::optional<gl::ColorTarget> m_aa;  // FXAA output, RGBA8

    unsigned int m_vao = 0; // empty VAO; the vertex shader builds the triangle from gl_VertexID
    int m_width = 0;
    int m_height = 0;
};

} // namespace rb
