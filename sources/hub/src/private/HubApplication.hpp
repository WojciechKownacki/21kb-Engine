#pragma once

#include "HubState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::hub {

class HubApplication {
public:
    HubApplication() = default;

    [[nodiscard]] bool Initialize(HINSTANCE instance, int showCommand);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void OpenCreateDialog(HWND window);
    void OpenExistingProject(HWND window);
    void LaunchSelectedProject(HWND window);
    void RemoveSelectedProject(HWND window);
    void RefreshProjects();
    void ClampScroll() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool comInitialized_ = false;
    HubState state_;
};

} // namespace kb::hub
