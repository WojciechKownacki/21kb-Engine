#include "kb/editor/EditorApplication.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "app/EditorSelfTest.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#endif

namespace {

#if defined(_WIN32)
void ConfigureProjectFromArguments(int argc, wchar_t** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view{ argv[index] } == L"--project") {
            kb::editor::EditorProjectPaths::SetProjectFile(std::filesystem::path{ argv[index + 1] });
            return;
        }
    }
}

[[nodiscard]] bool HasSelfTestFlag(int argc, wchar_t** argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view{ argv[index] } == L"--selftest") {
            return true;
        }
    }
    return false;
}
#else
void ConfigureProjectFromArguments(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{ argv[index] } == "--project") {
            kb::editor::EditorProjectPaths::SetProjectFile(argv[index + 1]);
            return;
        }
    }
}
#endif

int RunEditor() {
    kb::editor::EditorCrashBreadcrumbs::Write("app", "RunEditor begin");
    kb::editor::EditorApplication app;
    if (!app.Initialize()) {
        kb::editor::EditorCrashBreadcrumbs::Write("app", "EditorApplication Initialize failed");
        return 1;
    }

    kb::editor::EditorCrashBreadcrumbs::Write("app", "EditorApplication Run enter");
    app.Run();
    kb::editor::EditorCrashBreadcrumbs::Write("app", "EditorApplication Run leave");
    return 0;
}

} // namespace

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool selfTest = false;
    if (argv != nullptr) {
        ConfigureProjectFromArguments(argc, argv);
        selfTest = HasSelfTestFlag(argc, argv);
        LocalFree(argv);
    }
    kb::editor::EditorCrashBreadcrumbs::Reset();
    kb::editor::EditorCrashBreadcrumbs::InstallUnhandledExceptionLogger();
    kb::editor::EditorCrashBreadcrumbs::Write("app", "wWinMain enter");
    if (selfTest) {
        // Headless: run self-tests and exit before any window/graphics init.
        const std::filesystem::path reportPath = std::filesystem::temp_directory_path() / "21kb_selftest" / "report.txt";
        kb::editor::EditorCrashBreadcrumbs::Write("app", "selftest enter");
        return kb::editor::EditorSelfTest::Run(reportPath);
    }
    return RunEditor();
}
#else
int main(int argc, char** argv) {
    ConfigureProjectFromArguments(argc, argv);
    kb::editor::EditorCrashBreadcrumbs::Reset();
    kb::editor::EditorCrashBreadcrumbs::InstallUnhandledExceptionLogger();
    kb::editor::EditorCrashBreadcrumbs::Write("app", "main enter");
    return RunEditor();
}
#endif
