#include "kb/editor/EditorApplication.hpp"

#include "project/EditorProjectPaths.hpp"

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
    kb::editor::EditorApplication app;
    if (!app.Initialize()) {
        return 1;
    }

    app.Run();
    return 0;
}

} // namespace

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        ConfigureProjectFromArguments(argc, argv);
        LocalFree(argv);
    }
    return RunEditor();
}
#else
int main(int argc, char** argv) {
    ConfigureProjectFromArguments(argc, argv);
    return RunEditor();
}
#endif
