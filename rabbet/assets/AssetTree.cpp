#include "rabbet/assets/AssetTree.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rb {
namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool nameLess(const std::string& a, const std::string& b) {
    return toLower(a) < toLower(b);
}

// Descends from `root`, creating a child folder for each component of `relativeDir`, and returns the
// folder node that should own a leaf living in that directory.
AssetTree& folderFor(AssetTree& root, const std::filesystem::path& relativeDir) {
    AssetTree* current = &root;
    for (const std::filesystem::path& part : relativeDir) {
        const std::string component = part.string();
        if (component.empty() || component == ".") {
            continue;
        }
        auto it = std::find_if(current->folders.begin(), current->folders.end(),
                               [&](const AssetTree& f) { return f.name == component; });
        if (it != current->folders.end()) {
            current = &*it;
            continue;
        }
        AssetTree child;
        child.name = component;
        child.path =
            current->path.empty() ? std::filesystem::path(component) : current->path / component;
        current->folders.push_back(std::move(child));
        current = &current->folders.back();
    }
    return *current;
}

void sortTree(AssetTree& node) {
    std::sort(node.folders.begin(), node.folders.end(),
              [](const AssetTree& a, const AssetTree& b) { return nameLess(a.name, b.name); });
    std::sort(node.assets.begin(), node.assets.end(),
              [](const AssetTree::Leaf& a, const AssetTree::Leaf& b) {
                  return nameLess(a.name, b.name);
              });
    for (AssetTree& child : node.folders) {
        sortTree(child);
    }
}

} // namespace

AssetTree buildAssetTree(const std::filesystem::path& root,
                         std::span<const AssetDatabase::Record> records) {
    AssetTree tree;
    for (const AssetDatabase::Record& record : records) {
        // Lexical only (no filesystem access) so the builder is pure + headless-testable. A path
        // outside the root yields an empty or ".."-prefixed relative path; those are filed at the
        // root under just the filename.
        std::filesystem::path rel = record.path.lexically_relative(root);
        const bool relatable =
            rel.begin() != rel.end() && rel.begin()->string() != "..";
        if (!relatable) {
            rel = record.path.filename();
        }

        AssetTree& folder = folderFor(tree, rel.parent_path());
        AssetTree::Leaf leaf;
        leaf.id = record.id;
        leaf.type = record.type;
        leaf.name = record.name.empty() ? record.path.stem().string() : record.name;
        leaf.path = record.path;
        folder.assets.push_back(std::move(leaf));
    }
    sortTree(tree);
    return tree;
}

} // namespace rb
