#pragma once

#include <array>
#include <cstddef>

struct GLFWwindow;

namespace rb {

enum class Key {
    Space,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Escape, Enter, Tab, Backspace,
    Right, Left, Down, Up,
    LeftShift, LeftControl, LeftAlt,
    Count
};

enum class MouseButton { Left, Right, Middle, Count };

class Input {
public:
    explicit Input(GLFWwindow* window) noexcept;

    void update() noexcept;

    [[nodiscard]] bool keyDown(Key key) const noexcept;
    [[nodiscard]] bool keyPressed(Key key) const noexcept;
    [[nodiscard]] bool keyReleased(Key key) const noexcept;

    [[nodiscard]] bool mouseDown(MouseButton button) const noexcept;
    [[nodiscard]] bool mousePressed(MouseButton button) const noexcept;
    [[nodiscard]] bool mouseReleased(MouseButton button) const noexcept;

    [[nodiscard]] double cursorX() const noexcept { return m_cursorX; }
    [[nodiscard]] double cursorY() const noexcept { return m_cursorY; }
    [[nodiscard]] double cursorDeltaX() const noexcept { return m_cursorX - m_prevCursorX; }
    [[nodiscard]] double cursorDeltaY() const noexcept { return m_cursorY - m_prevCursorY; }

private:
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::Count);

    GLFWwindow* m_window;
    std::array<bool, kKeyCount> m_keys{};
    std::array<bool, kKeyCount> m_prevKeys{};
    std::array<bool, kButtonCount> m_buttons{};
    std::array<bool, kButtonCount> m_prevButtons{};
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;
    double m_prevCursorX = 0.0;
    double m_prevCursorY = 0.0;
};

} // namespace rb
