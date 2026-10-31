#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32RawMouseRegistration.hpp"
#include "engine/input/InputDeviceState.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
kb::input::Win32InputCollector collector;
LONG rawX = 0, rawY = 0;
unsigned int rawEvents = 0;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Pump() {
    const auto end = GetTickCount64() + 60;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < end);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INPUT) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size,
                sizeof(RAWINPUTHEADER)) != UINT(-1) && raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            rawX += raw.data.mouse.lLastX;
            rawY += raw.data.mouse.lLastY;
            ++rawEvents;
        }
    }
    if (collector.HandleWindowMessage(window, message, wparam, lparam)) return 0;
    return DefWindowProcW(window, message, wparam, lparam);
}
void Send(HWND window, INPUT input) {
    Require(GetForegroundWindow() == window, "test window lost foreground; refusing injected input");
    Require(SendInput(1, &input, sizeof(input)) == 1, "SendInput failed");
    Pump();
}
void Focus(HWND window) {
    SetForegroundWindow(window);
    Pump();
    if (GetForegroundWindow() == window) return;
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    POINT inside{20, 20};
    Require(ClientToScreen(window, &inside) != 0, "cannot locate test window");
    SetCursorPos(inside.x, inside.y);
    Require(WindowFromPoint(inside) == window, "another window covers the test; refusing activation click");
    INPUT click[2]{};
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    Require(SendInput(2, click, sizeof(INPUT)) == 2, "test activation click failed");
    Pump();
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Require(GetForegroundWindow() == window, "cannot acquire test foreground");
}
}

int main() {
    const HWND originalForeground = GetForegroundWindow();
    POINT originalCursor{};
    GetCursorPos(&originalCursor);
    const HKL originalLayout = GetKeyboardLayout(0);
    const auto originalDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = WindowProc;
    type.lpszClassName = L"EnginePointerHostTest";
    const ATOM registered = RegisterClassW(&type);
    if (!registered) return 1;
    HWND window = CreateWindowExW(0, type.lpszClassName, L"Input test", WS_OVERLAPPEDWINDOW,
        100, 100, 400, 300, nullptr, nullptr, type.hInstance, nullptr);
    int result = 1;
    WORD heldScan = 0;
    std::vector<HKL> addedLayouts;
    std::vector<HKL> originalLayouts(static_cast<std::size_t>(GetKeyboardLayoutList(0, nullptr)));
    if (!originalLayouts.empty()) GetKeyboardLayoutList(static_cast<int>(originalLayouts.size()), originalLayouts.data());
    kb::input::Win32RawMouseRegistration rawMouse;
    try {
        Require(window != nullptr, "cannot create test host");
        Require(rawMouse.Register(window), "cannot register raw mouse");
        kb::input::Win32RawMouseRegistration competing;
        const HWND other = CreateWindowExW(0, L"STATIC", L"Registration test", WS_OVERLAPPED,
            0, 0, 20, 20, nullptr, nullptr, type.hInstance, nullptr);
        const bool stolen = competing.Register(other);
        competing.Reset();
        DestroyWindow(other);
        Require(!stolen, "another host replaced the active mouse registration");
        ShowWindow(window, SW_SHOWNORMAL);
        Focus(window);
        kb::input::InputDeviceState state;
        collector.Collect(state, window);
        rawX = rawY = 0; rawEvents = 0;
        INPUT motion{};
        motion.type = INPUT_MOUSE;
        motion.mi.dx = 13; motion.mi.dy = -9;
        motion.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
        Send(window, motion);
        collector.Collect(state, window);
        Require(rawEvents != 0 && (rawX != 0 || rawY != 0), "OS did not deliver relative raw mouse events");
        using kb::input::InputKey;
        Require(state.GetValue(InputKey::MouseX) == static_cast<float>(rawX) &&
            state.GetValue(InputKey::MouseY) == static_cast<float>(rawY), "raw mouse counts did not reach collector");
        collector.Collect(state, window);
        Require(state.GetValue(InputKey::MouseX) == 0 && state.GetValue(InputKey::MouseY) == 0, "raw motion repeated next frame");
        collector.ConfigurePointerViewport(window, RECT{20, 30, 220, 130}, 400, 200);
        POINT target{120, 80};
        ClientToScreen(window, &target);
        SetCursorPos(target.x, target.y);
        Pump();
        collector.Collect(state, window);
        Require(std::abs(state.PointerX() - 200.0F) < .01F && std::abs(state.PointerY() - 100.0F) < .01F,
            "viewport coordinates do not match render pixels");
        Require(state.GetValue(InputKey::MouseX) == 0 && state.GetValue(InputKey::MouseY) == 0, "cursor warp became relative movement");
        for (const LONG scale : {75, 100, 150, 200}) {
            collector.ConfigurePointerViewport(window, RECT{20, 30, 20 + scale * 2, 30 + scale}, 400, 200);
            collector.Collect(state, window);
            Require(std::abs(state.PointerX() - 20000.0F / static_cast<float>(scale)) < .01F &&
                std::abs(state.PointerY() - 10000.0F / static_cast<float>(scale)) < .01F,
                "scaled viewport pointer coordinates differ from render pixels");
        }
        for (const auto& layout : {L"00000409", L"00000407"}) {
            const HKL keyboard = LoadKeyboardLayoutW(layout, KLF_NOTELLSHELL);
            if (keyboard != nullptr && std::find(originalLayouts.begin(), originalLayouts.end(), keyboard) == originalLayouts.end())
                addedLayouts.push_back(keyboard);
            Require(keyboard != nullptr && ActivateKeyboardLayout(keyboard, 0) != nullptr, "cannot activate test keyboard layout");
            Pump();
            INPUT key{};
            key.type = INPUT_KEYBOARD;
            key.ki.wScan = 0x15;
            key.ki.dwFlags = KEYEVENTF_SCANCODE;
            Send(window, key);
            heldScan = key.ki.wScan;
            collector.Collect(state, window);
            const auto expected = LOWORD(reinterpret_cast<ULONG_PTR>(keyboard)) == 0x0409 ? InputKey::Y : InputKey::Z;
            Require(state.IsKeyDown(expected), "keyboard layout mapped the scan code to the wrong action key");
            key.ki.dwFlags |= KEYEVENTF_KEYUP;
            Send(window, key);
            heldScan = 0;
            collector.Collect(state, window);
            Require(!state.IsKeyDown(InputKey::Y) && !state.IsKeyDown(InputKey::Z), "layout key remained held");
        }
        INPUT key{};
        key.type = INPUT_KEYBOARD;
        key.ki.wScan = 0x1e;
        key.ki.dwFlags = KEYEVENTF_SCANCODE;
        Send(window, key);
        heldScan = key.ki.wScan;
        collector.Collect(state, window);
        Require(state.IsKeyDown(InputKey::A), "focus fixture key was not held");
        key.ki.dwFlags |= KEYEVENTF_KEYUP;
        Send(window, key);
        heldScan = 0;
        // Hold a non-text modifier across the switch: it cannot auto-repeat
        // characters into the other application while this test is scheduled.
        key.ki.wScan = 0x2a;
        key.ki.dwFlags = KEYEVENTF_SCANCODE;
        Send(window, key);
        heldScan = key.ki.wScan;
        collector.Collect(state, window);
        Require(state.IsKeyDown(InputKey::LeftShift), "focus fixture modifier was not held");
        Require(GetForegroundWindow() == window && (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
            (GetAsyncKeyState(VK_TAB) & 0x8000) == 0, "refusing Alt-Tab while foreground or modifier state changed");
        INPUT switchWindow[4]{};
        for (auto& event : switchWindow) event.type = INPUT_KEYBOARD;
        switchWindow[0].ki.wVk = switchWindow[3].ki.wVk = VK_MENU;
        switchWindow[1].ki.wVk = switchWindow[2].ki.wVk = VK_TAB;
        switchWindow[2].ki.dwFlags = switchWindow[3].ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT sent = SendInput(4, switchWindow, sizeof(INPUT));
        if (sent != 4) {
            SendInput(2, &switchWindow[2], sizeof(INPUT));
            throw std::runtime_error("Alt-Tab input sequence failed");
        }
        Pump();
        collector.Collect(state, window);
        const bool modifierHeldDuringFocusLoss = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        key.ki.dwFlags |= KEYEVENTF_KEYUP;
        Require(SendInput(1, &key, sizeof(key)) == 1, "cannot release test modifier");
        heldScan = 0;
        Require(modifierHeldDuringFocusLoss && !state.HasFocus() && !state.IsKeyDown(InputKey::A) &&
            !state.IsKeyDown(InputKey::LeftShift) &&
            state.GetValue(InputKey::MouseX) == 0 && state.GetValue(InputKey::MouseY) == 0,
            "focus loss retained input");
        Focus(window);
        collector.Collect(state, window);
        Require(state.HasFocus() && !state.IsKeyDown(InputKey::A) && !state.IsKeyDown(InputKey::LeftShift) &&
            state.GetValue(InputKey::MouseX) == 0 && state.GetValue(InputKey::MouseY) == 0,
            "focus recovery replayed stale input");
        std::cout << "PASS: native raw mouse, frame consumption, four viewport scales, cursor warp, two keyboard layouts and Alt-Tab focus recovery\n";
        result = 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    if (heldScan != 0) {
        INPUT release{};
        release.type = INPUT_KEYBOARD;
        release.ki.wScan = heldScan;
        release.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        SendInput(1, &release, sizeof(release));
    }
    ActivateKeyboardLayout(originalLayout, 0);
    for (HKL keyboard : addedLayouts) UnloadKeyboardLayout(keyboard);
    rawMouse.Reset();
    if (window) DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
    if (originalDpi) SetThreadDpiAwarenessContext(originalDpi);
    SetCursorPos(originalCursor.x, originalCursor.y);
    if (IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
    return result;
}
