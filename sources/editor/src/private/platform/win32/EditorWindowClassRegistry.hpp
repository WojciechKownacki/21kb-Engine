#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace kb::editor {

class EditorWindowClassRegistry {
public:
    static constexpr const wchar_t* MainWindowClassName = L"KBEditorWindow";

    EditorWindowClassRegistry() = default;

    [[nodiscard]] bool Register(HINSTANCE instance, WNDPROC windowProc) noexcept;
    void Unregister() noexcept;

private:
    [[nodiscard]] bool RegisterClass(HINSTANCE instance, WNDPROC windowProc, const wchar_t* className, const char* errorLabel) noexcept;

    HINSTANCE instance_ = nullptr;
    bool registeredMain_ = false;
    bool registeredFloating_ = false;
};

} // namespace kb::editor

#endif
