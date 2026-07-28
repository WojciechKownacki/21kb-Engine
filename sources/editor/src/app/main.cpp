#include "kb/editor/EditorApplication.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "app/EditorSelfTest.hpp"
#include "project/EditorProjectPaths.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <filesystem>
#include <optional>
#include <string>
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

[[nodiscard]] std::optional<std::wstring_view> ArgumentValue(
    int argc, wchar_t** argv, std::wstring_view name) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view{ argv[index] } == name) {
            return std::wstring_view{ argv[index + 1] };
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool ValidTaskName(std::wstring_view name) noexcept {
    if (name.empty() || name.size() > 96U) return false;
    for (const wchar_t value : name) {
        if (!((value >= L'a' && value <= L'z') ||
              (value >= L'A' && value <= L'Z') ||
              (value >= L'0' && value <= L'9') ||
              value == L'-' || value == L'_')) {
            return false;
        }
    }
    return true;
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
    std::filesystem::path selfTestRoot =
        std::filesystem::current_path() / "SelfTest";
    std::wstring selfTestTask =
        L"SELFTEST-001-Headless-Editor-Automation";
    if (argv != nullptr) {
        ConfigureProjectFromArguments(argc, argv);
        selfTest = HasSelfTestFlag(argc, argv);
        if (const auto root = ArgumentValue(
                argc, argv, L"--selftest-root")) {
            selfTestRoot = std::filesystem::path{
                std::wstring{ root->begin(), root->end() } };
        }
        if (const auto task = ArgumentValue(
                argc, argv, L"--selftest-task")) {
            selfTestTask.assign(task->begin(), task->end());
        }
        LocalFree(argv);
    }
    kb::editor::EditorCrashBreadcrumbs::Reset();
    kb::editor::EditorCrashBreadcrumbs::InstallUnhandledExceptionLogger();
    kb::editor::EditorCrashBreadcrumbs::Write("app", "wWinMain enter");
    if (selfTest) {
        // Headless: run self-tests and exit before any window/graphics init.
        if (!ValidTaskName(selfTestTask)) {
            return 2;
        }
        const std::filesystem::path artifactRoot =
            std::filesystem::absolute(selfTestRoot) / selfTestTask;
        const std::filesystem::path reportPath =
            artifactRoot / "report.txt";
        kb::editor::EditorCrashBreadcrumbs::Write("app", "selftest enter");
        return kb::editor::EditorSelfTest::Run(
            reportPath, artifactRoot);
    }
    // Interactive editor: stream textures in the background so picking a texture / opening a material never
    // freezes the render thread on a first-time decode. (Off in the self-test above and in tests, where
    // synchronous, deterministic binding is wanted.)
    kb::render::RenderTextureAssetLoader::SetAsyncTextureDecodeEnabled(true);
    return RunEditor();
}
#else
int main(int argc, char** argv) {
    ConfigureProjectFromArguments(argc, argv);
    kb::editor::EditorCrashBreadcrumbs::Reset();
    kb::editor::EditorCrashBreadcrumbs::InstallUnhandledExceptionLogger();
    kb::editor::EditorCrashBreadcrumbs::Write("app", "main enter");
    kb::render::RenderTextureAssetLoader::SetAsyncTextureDecodeEnabled(true);
    return RunEditor();
}
#endif
