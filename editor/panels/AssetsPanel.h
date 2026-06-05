#pragma once

#include "editor/Panel.h"

#include "rabbet/core/Uuid.h"

namespace rb::editor {

// Browses the project's assets from the AssetDatabase resource, grouped by type, and
// assigns the selected asset to the selected entity (a Model binds its ModelRenderer,
// importing on demand so AssetResolveSystem can resolve the reference).
class AssetsPanel final : public Panel {
public:
    explicit AssetsPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Assets"; }
    void onImGui() override;

private:
    rb::Uuid m_selected;
};

} // namespace rb::editor
