// Drives the real kb_game executable end to end: it authors a project the way
// the editor persists one, launches the shipped binary against it with a bounded
// frame count, and checks that the process both exited cleanly and actually
// brought the configured scene up - a started process is not evidence.

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

constexpr DWORD kProcessTimeoutMilliseconds = 120000U;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fputs(message, stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_kb_game_tests";
}

[[nodiscard]] bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

[[nodiscard]] unsigned long FramesReported(const std::string& text) {
    const std::size_t marker = text.find("frames=");
    Require(marker != std::string::npos, "kb_game did not report a frame count");
    return std::strtoul(text.c_str() + marker + 7U, nullptr, 10);
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    Require(!error, "kb_game test directory could not be created");
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "kb_game test file could not be opened");
    output << text;
    Require(output.good(), "kb_game test file could not be written");
}

// Writes a scene with `entityCount` named roots, the first of which runs the
// project's Lua behaviour, and reports the entity count the engine ends up with
// when that file is loaded - which is what kb_game prints.
[[nodiscard]] std::size_t WriteScene(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& path,
    const std::string& name,
    const std::string& behaviourVirtualPath,
    std::size_t entityCount) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    Require(!error, "kb_game test scene directory could not be created");

    kb::scene::Scene authored;
    Require(
        authored.Assets().MountProject(projectRoot),
        "kb_game test project could not be mounted for authoring");
    static_cast<void>(authored.Assets().Discover());
    const kb::assets::AssetMetadata* behaviour =
        authored.Assets().Manager().Registry().FindByPath(behaviourVirtualPath);
    Require(behaviour != nullptr, "kb_game test behaviour asset was not discovered");

    for (std::size_t index = 0U; index < entityCount; ++index) {
        const kb::scene::SceneObject object =
            authored.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = name + "_" + std::to_string(index),
            });
        if (index == 0U) {
            authored.Components().Behaviours().Set(
                object.Entity(),
                kb::scene::BehaviourComponent{
                    .behaviourAssetId = behaviour->id.value,
                    .backend = kb::scene::BehaviourBackend::Lua,
                });
        }
    }
    Require(
        kb::scene::SceneDocumentService::Save(authored, path, name),
        "kb_game test scene could not be saved");

    kb::scene::Scene reloaded;
    Require(
        kb::scene::SceneDocumentService::LoadFileIntoScene(reloaded, path),
        "kb_game test scene could not be read back");
    return reloaded.Entities().Count();
}

struct ProcessRun {
    DWORD exitCode = 0U;
    std::string output;
};

struct LaunchedProcess {
    HANDLE process = nullptr;
    HANDLE log = nullptr;
};

[[nodiscard]] LaunchedProcess LaunchGame(
    const std::wstring& arguments, const std::filesystem::path& logPath) {
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    const HANDLE log = CreateFileW(
        logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    Require(log != INVALID_HANDLE_VALUE, "kb_game test output file could not be created");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = log;
    startup.hStdError = log;

    // kb_game is a windowed binary, so it is started directly rather than through
    // a shell: a command interpreter does not wait for a GUI subsystem process.
    std::wstring commandLine =
        L"\"" + std::filesystem::path{ KB_GAME_EXECUTABLE_PATH }.wstring() + L"\" " + arguments;
    const std::filesystem::path executableDirectory =
        std::filesystem::path{ KB_GAME_EXECUTABLE_PATH }.parent_path();

    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        0U,
        nullptr,
        executableDirectory.c_str(),
        &startup,
        &process);
    Require(started != 0, "kb_game executable could not be started");
    static_cast<void>(CloseHandle(process.hThread));
    return LaunchedProcess{ .process = process.hProcess, .log = log };
}

[[nodiscard]] ProcessRun FinishGame(
    const LaunchedProcess& launched, const std::filesystem::path& logPath) {
    const DWORD waited = WaitForSingleObject(launched.process, kProcessTimeoutMilliseconds);
    if (waited != WAIT_OBJECT_0) {
        static_cast<void>(TerminateProcess(launched.process, 258U));
        static_cast<void>(WaitForSingleObject(launched.process, 5000U));
    }
    ProcessRun run{};
    Require(
        GetExitCodeProcess(launched.process, &run.exitCode) != 0,
        "kb_game exit code could not be read");
    static_cast<void>(CloseHandle(launched.process));
    static_cast<void>(CloseHandle(launched.log));
    Require(waited == WAIT_OBJECT_0, "kb_game did not exit within the test timeout");

    std::ifstream captured{ logPath, std::ios::binary };
    Require(captured.is_open(), "kb_game test output file could not be read");
    run.output.assign(
        std::istreambuf_iterator<char>{ captured }, std::istreambuf_iterator<char>{});
    return run;
}

[[nodiscard]] ProcessRun RunGame(const std::wstring& arguments, const std::filesystem::path& logPath) {
    return FinishGame(LaunchGame(arguments, logPath), logPath);
}

struct WindowSearch {
    DWORD processId = 0U;
    HWND window = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM context) {
    auto* search = reinterpret_cast<WindowSearch*>(context);
    DWORD owner = 0U;
    static_cast<void>(GetWindowThreadProcessId(window, &owner));
    if (owner == search->processId && IsWindowVisible(window) != 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

// Launches the game the way a player does - no frame budget - lets it run, then
// closes its window and checks that the loop survived until asked to stop.
[[nodiscard]] ProcessRun RunGameUntilWindowClosed(
    const std::wstring& arguments, const std::filesystem::path& logPath) {
    LaunchedProcess launched = LaunchGame(arguments, logPath);

    WindowSearch search{ .processId = GetProcessId(launched.process), .window = nullptr };
    for (int attempt = 0; attempt < 300 && search.window == nullptr; ++attempt) {
        Sleep(50U);
        Require(
            WaitForSingleObject(launched.process, 0U) != WAIT_OBJECT_0,
            "kb_game exited before it opened a window");
        static_cast<void>(EnumWindows(&FindProcessWindow, reinterpret_cast<LPARAM>(&search)));
    }
    Require(search.window != nullptr, "kb_game did not open a window");

    // Long enough that a loop which only ran a single frame is distinguishable
    // from one that keeps running until the player closes it.
    Sleep(1000U);
    Require(
        WaitForSingleObject(launched.process, 0U) != WAIT_OBJECT_0,
        "kb_game exited on its own while its window was still open");
    Require(
        PostMessageW(search.window, WM_CLOSE, 0U, 0U) != 0,
        "kb_game window could not be asked to close");
    return FinishGame(launched, logPath);
}

void Report(const char* label, const ProcessRun& run) {
    std::fprintf(stdout, "--- %s (exit %lu) ---\n%s\n", label, run.exitCode, run.output.c_str());
    std::fflush(stdout);
}

} // namespace

int main() {
    const std::filesystem::path root = TestRoot();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "kb_game test root could not be prepared");

    // A behaviour the game actually compiles and ticks, so the launch check
    // covers script runtime startup and teardown, not just an empty scene.
    WriteTextFile(root / "Assets" / "Logic" / "Player.lua", R"(
function Tick(self, dt)
    local x = self:GetProperty("Transform", "localPosition.x")
    self:SetProperty("Transform", "localPosition.x", x + dt)
end
)");

    // No plugins: the launch check must prove the runtime host, not a provider.
    const kb::project::ProjectDescriptor descriptor;
    Require(
        kb::project::ProjectManager::SaveProject(root / "Project.21kbproject", descriptor),
        "kb_game test project descriptor could not be written");

    const std::size_t defaultSceneEntities = WriteScene(
        root, root / "Assets" / "Scenes" / "Main.21kbscene", "Main", "/Game/Logic/Player.lua", 3U);
    const std::size_t alternateSceneEntities = WriteScene(
        root,
        root / "Assets" / "Scenes" / "Alternate.21kbscene",
        "Alternate",
        "/Game/Logic/Player.lua",
        7U);
    Require(
        defaultSceneEntities != alternateSceneEntities,
        "kb_game test scenes must differ so a scene override is observable");

    kb::project::ProjectSettings settings;
    settings.name = "KbGameLaunchTest";
    settings.gameName = "21kb Launch Test";
    settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    std::string settingsError;
    Require(
        kb::project::ProjectSettingsStore::Save(
            kb::project::ProjectSettingsStore::FilePath(root), settings, settingsError),
        "kb_game test project settings could not be written");

    const std::wstring projectArgument = L"--project=\"" + root.wstring() + L"\" ";

    const ProcessRun defaultMap =
        RunGame(projectArgument + L"--frames=3", root / "default_map.log");
    Report("default map", defaultMap);
    Require(defaultMap.exitCode == 0U, "kb_game did not exit successfully on its default map");
    Require(
        Contains(defaultMap.output, "Main.21kbscene"),
        "kb_game did not report loading the project's defaultMap");
    Require(
        Contains(defaultMap.output, "entities=" + std::to_string(defaultSceneEntities)),
        "kb_game did not instantiate the default map's entities");
    Require(
        Contains(defaultMap.output, "frames=3 shutdown=clean"),
        "kb_game did not run the requested frames and shut down cleanly");

    const ProcessRun overridden = RunGame(
        projectArgument + L"--scene=/Game/Scenes/Alternate.21kbscene --frames=2",
        root / "override.log");
    Report("scene override", overridden);
    Require(overridden.exitCode == 0U, "kb_game did not exit successfully on an overridden scene");
    Require(
        Contains(overridden.output, "Alternate.21kbscene"),
        "kb_game did not honour the --scene override");
    Require(
        Contains(overridden.output, "entities=" + std::to_string(alternateSceneEntities)),
        "kb_game did not instantiate the overridden scene's entities");
    Require(
        Contains(overridden.output, "frames=2 shutdown=clean"),
        "kb_game did not run the requested frames on the overridden scene");

    // The shipping behaviour: no frame budget at all, the loop runs until the
    // player closes the window, and the teardown that follows must be clean.
    const ProcessRun closed = RunGameUntilWindowClosed(projectArgument, root / "closed.log");
    Report("window close", closed);
    Require(closed.exitCode == 0U, "kb_game did not exit cleanly when its window was closed");
    Require(
        Contains(closed.output, "entities=" + std::to_string(defaultSceneEntities)),
        "kb_game did not load the default map on an unbounded run");
    Require(
        FramesReported(closed.output) > 1U,
        "kb_game stopped on its own instead of running until the window closed");
    Require(
        Contains(closed.output, "shutdown=clean"),
        "kb_game did not complete its shutdown after the window closed");

    const ProcessRun missing = RunGame(
        projectArgument + L"--scene=/Game/Scenes/Absent.21kbscene --frames=1",
        root / "missing.log");
    Report("missing scene", missing);
    Require(missing.exitCode != 0U, "kb_game reported success for a scene that does not exist");
    Require(
        Contains(missing.output, "project scene asset was not found"),
        "kb_game did not name the missing scene asset");

    std::fputs("kb_game launch tests passed\n", stdout);
    return EXIT_SUCCESS;
}
