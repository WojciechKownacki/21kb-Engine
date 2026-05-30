#include "platform/win32/EditorWindowClassRegistry.hpp"

#if defined(_WIN32)
#include "docking/EditorFloatingWindowManager.hpp"
#include "platform/win32/Win32ErrorReporter.hpp"

namespace kb::editor {

bool EditorWindowClassRegistry::Register(HINSTANCE instance, WNDPROC windowProc) noexcept {
    instance_ = instance;
    registeredMain_ = RegisterClass(instance, windowProc, MainWindowClassName, "RegisterClassExW");
    registeredFloating_ = RegisterClass(instance, windowProc, EditorFloatingWindowManager::WindowClassName, "RegisterClassExW floating");
    return registeredMain_ && registeredFloating_;
}

void EditorWindowClassRegistry::Unregister() noexcept {
    if (instance_ == nullptr) {
        return;
    }

    if (registeredFloating_) {
        UnregisterClassW(EditorFloatingWindowManager::WindowClassName, instance_);
        registeredFloating_ = false;
    }
    if (registeredMain_) {
        UnregisterClassW(MainWindowClassName, instance_);
        registeredMain_ = false;
    }
    instance_ = nullptr;
}

bool EditorWindowClassRegistry::RegisterClass(HINSTANCE instance, WNDPROC windowProc, const wchar_t* className, const char* errorLabel) noexcept {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = className;

    if (RegisterClassExW(&windowClass) != 0) {
        return true;
    }

    if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        return true;
    }

    Win32ErrorReporter::PrintLastError(errorLabel);
    return false;
}

} // namespace kb::editor

#endif
