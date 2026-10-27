#include "engine/platform/win32/Win32ClipboardText.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <iostream>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int RunWithIsolatedLogon() {
    // A standard user cannot name a new window station. A fresh logon session
    // gives its unnamed station a unique name while retaining the caller's
    // local token. Dummy network credentials are never used for network IO.
    wchar_t executable[32768]{};
    Require(GetModuleFileNameW(nullptr, executable, 32768U) != 0U, "Cannot locate clipboard test executable");
    wchar_t directory[MAX_PATH]{};
    wchar_t report[MAX_PATH]{};
    Require(GetTempPathW(MAX_PATH, directory) != 0U && GetTempFileNameW(directory, L"kbc", 0U, report) != 0U,
        "Cannot create clipboard test report");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --isolated-logon \"" + report + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\default";
    startup.lpDesktop = desktop;
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessWithLogonW(L"ClipboardTest", L".", L"unused",
        LOGON_NETCREDENTIALS_ONLY, executable, command.data(), CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE;
    const auto launchError = GetLastError();
    DWORD code = 1U;
    if (launched) {
        if (WaitForSingleObject(process.hProcess, 20000U) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 124U);
            WaitForSingleObject(process.hProcess, 5000U);
        }
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else std::cerr << "Isolated logon launch failed with Win32 error " << launchError << '\n';
    std::ifstream diagnostics{std::filesystem::path{report}};
    for (std::string line; std::getline(diagnostics, line);) std::cerr << line << '\n';
    diagnostics.close();
    DeleteFileW(report);
    if (code == 0U) std::cout << "Isolated native and runtime clipboard tests passed\n";
    return code == 0U ? 0 : 1;
}

// A noninteractive window station has its own clipboard. These tests never read
// or replace the interactive desktop's clipboard and never display a window.
struct IsolatedClipboard {
    HWINSTA previousStation = GetProcessWindowStation();
    HDESK previousDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    HWND window = nullptr;
    const char* failedOperation = "CreateWindowStation";
    DWORD error = ERROR_SUCCESS;
    IsolatedClipboard() {
        const auto name = L"21kbClipboardTests-" + std::to_wstring(GetCurrentProcessId());
        station = CreateWindowStationW(name.c_str(), CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, nullptr);
        if (station == nullptr)
            station = CreateWindowStationW(nullptr, CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, nullptr);
        if (station == nullptr) { error = GetLastError(); return; }
        failedOperation = "SetProcessWindowStation";
        if (!SetProcessWindowStation(station)) { error = GetLastError(); return; }
        failedOperation = "CreateDesktop";
        desktop = CreateDesktopW(L"ClipboardTests", nullptr, nullptr, 0U,
            DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
        if (desktop == nullptr) { error = GetLastError(); return; }
        failedOperation = "SetThreadDesktop";
        if (!SetThreadDesktop(desktop)) { error = GetLastError(); return; }
        failedOperation = "CreateWindowEx";
        window = CreateWindowExW(0U, L"STATIC", L"ClipboardTests", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window == nullptr) error = GetLastError();
    }
    ~IsolatedClipboard() {
        if (window != nullptr) DestroyWindow(window);
        if (previousStation != nullptr) SetProcessWindowStation(previousStation);
        if (previousDesktop != nullptr) SetThreadDesktop(previousDesktop);
        if (desktop != nullptr) CloseDesktop(desktop);
        if (station != nullptr) CloseWindowStation(station);
    }
};

void VerifyNativeClipboard(HWND window) {
    using namespace kb::platform;
    const std::string text = "A\xC5\xBC\xF0\x9F\x98\x80\n";
    Require(WriteWin32ClipboardText(window, text) && ReadWin32ClipboardText(window) == text,
        "Unicode and line breaks must round-trip through the native clipboard");
    Require(ReadWin32ClipboardText(window, 2U) == "A" && ReadWin32ClipboardText(window, 4U) == "A\xC5\xBC",
        "Clipboard byte limits must not split a UTF-8 character");
    Require(!WriteWin32ClipboardText(window, "\xC0\xAF") && ReadWin32ClipboardText(window) == text,
        "Invalid UTF-8 must be refused before replacing existing clipboard contents");
    Require(!WriteWin32ClipboardText(window, std::string_view{"a\0b", 3U}), "Embedded nulls must be refused");
    Require(WriteWin32ClipboardText(window, "") && ReadWin32ClipboardText(window) == "",
        "An empty native text value must round-trip");
    const std::string limit(1024U * 1024U, 'x');
    Require(WriteWin32ClipboardText(window, limit) && ReadWin32ClipboardText(window) == limit,
        "The transfer limit must include room for the native terminator");
    Require(!WriteWin32ClipboardText(window, limit + "x") && ReadWin32ClipboardText(window, 1U) == "x",
        "An oversized transfer must preserve the previous clipboard value");
}

void VerifyRuntimeClipboard(HWND window) {
    using namespace kb::scene;
    using kb::input::InputKey;
    Scene scene{SceneMode::PrefabPrivate};
    const auto canvas = scene.Entities().CreateObject({});
    auto canvasComponents = BuildUIComponentPreset(UIComponentPreset::Canvas);
    ApplySceneUIComponents(scene.Components().UI(), canvas.Entity(), canvasComponents);
    const auto field = scene.Entities().CreateObject(SceneObjectDesc{.parent = canvas});
    const auto components = BuildUIComponentPreset(UIComponentPreset::InputField);
    ApplySceneUIComponents(scene.Components().UI(), field.Entity(), components);
    const auto update = [&] { Require(scene.UI().Update(640.0F, 480.0F, {}, 0.016F), "UI update failed"); };
    update();
    Require(scene.UI().SetFocus(field.Entity()), "Clipboard fixture focus failed");
    auto& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    kb::input::Win32InputCollector collector;
    const auto press = [&](InputKey key) {
        device.SetKeyDown(InputKey::LeftControl, true);
        device.SetKeyDown(key, true);
        collector.ApplyTextClipboard(scene, window);
        update();
        device.SetKeyDown(key, false);
        device.SetKeyDown(InputKey::LeftControl, false);
        collector.ApplyTextClipboard(scene, window);
        update();
    };
    Require(scene.UI().QueueTextInputState({field.Entity(), U"A\U0001F600B", 2U, 1U}), "Selection fixture failed");
    update();
    press(InputKey::C);
    Require(kb::platform::ReadWin32ClipboardText(window) == "\xF0\x9F\x98\x80",
        "Runtime copy must copy only the selected Unicode text");
    Require(kb::platform::WriteWin32ClipboardText(window, "\xC5\xBCQ"), "Paste fixture failed");
    press(InputKey::V);
    Require(scene.UI().TextInputState()->text == U"A\u017CQB", "Runtime paste must replace the selection");
    Require(scene.UI().QueueTextInputState({field.Entity(), U"A\u017CQB", 4U, 0U}), "Cut fixture failed");
    update();
    press(InputKey::X);
    Require(scene.UI().TextInputState()->text.empty() && kb::platform::ReadWin32ClipboardText(window) == "A\xC5\xBCQB",
        "Runtime cut must copy and remove the same selected text");
    Require(scene.UI().QueueTextInputState({field.Entity(), U"secret", 6U, 0U}), "Password fixture failed");
    update();
    scene.Components().UI().TryGet<UIInputField>(field.Entity())->contentType = UIInputContentType::Password;
    Require(kb::platform::WriteWin32ClipboardText(window, "unchanged"), "Protected clipboard fixture failed");
    press(InputKey::C);
    Require(kb::platform::ReadWin32ClipboardText(window) == "unchanged", "Password copy must not expose field contents");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const bool isolatedLogon = argc == 3 && std::wstring_view{argv[1]} == L"--isolated-logon";
        if (isolatedLogon) {
            FILE* report = nullptr;
            Require(_wfreopen_s(&report, argv[2], L"w", stderr) == 0, "Cannot open clipboard test report");
        }
        IsolatedClipboard clipboard;
        if (clipboard.window == nullptr && !isolatedLogon) return RunWithIsolatedLogon();
        if (clipboard.window == nullptr)
            std::cerr << clipboard.failedOperation << " failed with Win32 error " << clipboard.error << '\n';
        Require(clipboard.window != nullptr, "An isolated clipboard station and hidden owner window are required");
        VerifyNativeClipboard(clipboard.window);
        VerifyRuntimeClipboard(clipboard.window);
        std::cout << "Isolated native and runtime clipboard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
