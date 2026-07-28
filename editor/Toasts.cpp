#include "editor/Toasts.h"

#include "editor/Palette.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace rb::editor {
namespace {

constexpr float kLifetime = 4.0f;
constexpr float kFadeOut = 0.6f;
constexpr float kWidth = 300.0f;
constexpr std::size_t kMaxItems = 5;

const ImVec4& accentFor(const Palette& p, ToastKind kind) {
    switch (kind) {
    case ToastKind::Success: return p.good;
    case ToastKind::Warning: return p.warn;
    case ToastKind::Error: return p.danger;
    case ToastKind::Info: break;
    }
    return p.info;
}

} // namespace

void Toasts::push(ToastKind kind, std::string text) {
    if (m_items.size() >= kMaxItems) {
        m_items.erase(m_items.begin());
    }
    m_items.push_back(Toast{kind, std::move(text), 0.0f});
}

void Toasts::tick(float dt) {
    // Native dialogs block the frame loop; the catch-up dt would age a toast pushed
    // from that very dialog to death before it is ever seen.
    dt = std::min(dt, 0.1f);
    for (Toast& toast : m_items) {
        toast.age += dt;
    }
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [](const Toast& t) { return t.age >= kLifetime; }),
                  m_items.end());
}

void Toasts::draw() {
    if (m_items.empty()) {
        return;
    }
    const Palette& p = palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##toasts", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove);
    int id = 0;
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) { // newest on top
        const Toast& toast = *it;
        const float fade =
            toast.age > kLifetime - kFadeOut ? (kLifetime - toast.age) / kFadeOut : 1.0f;

        ImVec4 fill = p.base;
        fill.w = 0.92f * fade;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, fill);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, p.roundTile);
        ImGui::PushID(id++);
        ImGui::BeginChild("##toast", ImVec2(kWidth, 0.0f),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);

        ImVec4 accent = accentFor(p, toast.kind);
        accent.w *= fade;
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 dotPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(dotPos.x + 4.0f, dotPos.y + lineHeight * 0.5f + 1.0f), 3.5f,
            ImGui::GetColorU32(accent));
        ImGui::Dummy(ImVec2(12.0f, lineHeight));
        ImGui::SameLine();

        ImVec4 ink = p.ink;
        ink.w *= fade;
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(toast.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
    ImGui::End();
}

} // namespace rb::editor
