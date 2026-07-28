#pragma once

#include <string>
#include <vector>

namespace rb::editor {

enum class ToastKind { Info, Success, Warning, Error };

// Transient overlay notifications: the console keeps the full log, a toast is the
// short-lived surface for "did that work" moments (saves, loads, prefab capture).
class Toasts {
public:
    void push(ToastKind kind, std::string text);
    void tick(float dt);
    void draw();

private:
    struct Toast {
        ToastKind kind = ToastKind::Info;
        std::string text;
        float age = 0.0f;
    };
    std::vector<Toast> m_items;
};

} // namespace rb::editor
