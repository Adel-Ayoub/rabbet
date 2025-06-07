#include "rabbet/platform/Input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace rb {
namespace {

static_assert(static_cast<int>(Key::Z) - static_cast<int>(Key::A) == 25, "letter keys must be contiguous");
static_assert(static_cast<int>(Key::Num9) - static_cast<int>(Key::Num0) == 9, "digit keys must be contiguous");

int glfwKeyCode(Key key) noexcept {
    const int offset = static_cast<int>(key);
    if (key >= Key::A && key <= Key::Z) {
        return GLFW_KEY_A + (offset - static_cast<int>(Key::A));
    }
    if (key >= Key::Num0 && key <= Key::Num9) {
        return GLFW_KEY_0 + (offset - static_cast<int>(Key::Num0));
    }
    switch (key) {
        case Key::Space: return GLFW_KEY_SPACE;
        case Key::Escape: return GLFW_KEY_ESCAPE;
        case Key::Enter: return GLFW_KEY_ENTER;
        case Key::Tab: return GLFW_KEY_TAB;
        case Key::Backspace: return GLFW_KEY_BACKSPACE;
        case Key::Right: return GLFW_KEY_RIGHT;
        case Key::Left: return GLFW_KEY_LEFT;
        case Key::Down: return GLFW_KEY_DOWN;
        case Key::Up: return GLFW_KEY_UP;
        case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
        default: return GLFW_KEY_UNKNOWN;
    }
}

int glfwButtonCode(MouseButton button) noexcept {
    switch (button) {
        case MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
        case MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        default: return GLFW_MOUSE_BUTTON_LAST + 1;
    }
}

} // namespace

Input::Input(GLFWwindow* window) noexcept : m_window(window) {}

void Input::update() noexcept {
    m_prevKeys = m_keys;
    m_prevButtons = m_buttons;

    for (std::size_t i = 0; i < kKeyCount; ++i) {
        const int code = glfwKeyCode(static_cast<Key>(i));
        m_keys[i] = code != GLFW_KEY_UNKNOWN && glfwGetKey(m_window, code) == GLFW_PRESS;
    }
    for (std::size_t i = 0; i < kButtonCount; ++i) {
        const int code = glfwButtonCode(static_cast<MouseButton>(i));
        m_buttons[i] = glfwGetMouseButton(m_window, code) == GLFW_PRESS;
    }

    m_prevCursorX = m_cursorX;
    m_prevCursorY = m_cursorY;
    glfwGetCursorPos(m_window, &m_cursorX, &m_cursorY);
}

bool Input::keyDown(Key key) const noexcept {
    return m_keys[static_cast<std::size_t>(key)];
}

bool Input::keyPressed(Key key) const noexcept {
    const std::size_t i = static_cast<std::size_t>(key);
    return m_keys[i] && !m_prevKeys[i];
}

bool Input::keyReleased(Key key) const noexcept {
    const std::size_t i = static_cast<std::size_t>(key);
    return !m_keys[i] && m_prevKeys[i];
}

bool Input::mouseDown(MouseButton button) const noexcept {
    return m_buttons[static_cast<std::size_t>(button)];
}

bool Input::mousePressed(MouseButton button) const noexcept {
    const std::size_t i = static_cast<std::size_t>(button);
    return m_buttons[i] && !m_prevButtons[i];
}

bool Input::mouseReleased(MouseButton button) const noexcept {
    const std::size_t i = static_cast<std::size_t>(button);
    return !m_buttons[i] && m_prevButtons[i];
}

} // namespace rb
