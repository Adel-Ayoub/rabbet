#pragma once

#include "editor/Panel.h"

#include "rabbet/core/Uuid.h"

#include <filesystem>

namespace rb::editor {

// Browses the project's assets from the AssetDatabase resource as a virtual-path folder tree with a
// thumbnail grid (or a compact list), and assigns the selected asset to the selected entity (a Model
// binds its ModelRenderer, importing on demand so AssetResolveSystem can resolve the reference). A
// prefab instantiates into a new entity instead.
class AssetsPanel final : public Panel {
public:
    explicit AssetsPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Assets"; }
    void onImGui() override;

private:
    rb::Uuid m_selected;
    std::filesystem::path m_folder; // current folder, relative to the database root ("" = root)
    bool m_grid = true;             // grid of thumbnails vs. a compact list
};

} // namespace rb::editor
