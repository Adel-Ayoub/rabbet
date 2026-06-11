#pragma once

namespace rb {

// Editor-toggled debug visualization flags, read by the RenderSystem. Lives as a Runtime
// resource so the render pass can check it without depending on the editor.
struct DebugDraw {
    bool colliders = true;
};

} // namespace rb
