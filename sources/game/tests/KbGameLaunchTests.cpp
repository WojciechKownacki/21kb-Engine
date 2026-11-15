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
#include "engine/scene/MeshRendererComponent.hpp"
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr DWORD kProcessTimeoutMilliseconds = 120000U;
constexpr DWORD kRefusalTimeoutMilliseconds = 30000U;

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

// Han, Cyrillic and Greek in one name: no single-byte code page can spell all
// three. kb_game keeps the real command line (CommandLineToArgvW), so every
// narrowing conversion on the way to a diagnostic is a place an exception can
// escape a windowed process - where it becomes abort() behind a modal dialog
// that neither a player nor an automated run can dismiss.
[[nodiscard]] std::wstring UnspellableName() {
    static constexpr wchar_t letters[] = { 0x65E5, 0x672C, 0x8A9E, 0x0416, 0x03A9, 0 };
    return std::wstring{ L"21kb_game_negator_" } + letters;
}

[[nodiscard]] unsigned long CountReported(const std::string& text, const std::string& marker) {
    const std::size_t at = text.find(marker);
    Require(at != std::string::npos, ("kb_game did not report " + marker).c_str());
    return std::strtoul(text.c_str() + at + marker.size(), nullptr, 10);
}

[[nodiscard]] double SecondsReported(const std::string& text, const std::string& marker) {
    const std::size_t at = text.find(marker);
    Require(at != std::string::npos, ("kb_game did not report " + marker).c_str());
    return std::strtod(text.c_str() + at + marker.size(), nullptr);
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

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    Require(input.is_open(), "kb_game test file could not be read");
    return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

// Every number that follows `"key":` in a JSON text, in order.
[[nodiscard]] std::vector<double> ReportedNumbers(const std::string& json, const std::string& key) {
    std::vector<double> values;
    const std::string marker = "\"" + key + "\":";
    for (std::size_t at = json.find(marker); at != std::string::npos; at = json.find(marker, at + marker.size())) {
        values.push_back(std::strtod(json.c_str() + at + marker.size(), nullptr));
    }
    return values;
}

// A one pixel PNG: the smallest image a launch screen can name.
constexpr unsigned char kOnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xFC, 0xCF, 0xC0, 0x50, 0x0F, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

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

// Writes a scene whose content is partly broken: one entity runs the healthy
// behaviour, one a behaviour that fails on every Tick, and one renders a mesh
// and a material that do not exist in the project.
void WriteDegradedScene(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& path,
    std::uint64_t missingAssetId) {
    kb::scene::Scene authored;
    Require(
        authored.Assets().MountProject(projectRoot),
        "kb_game degraded test project could not be mounted for authoring");
    static_cast<void>(authored.Assets().Discover());
    const auto behaviour = [&](const char* name, const char* virtualPath) {
        const kb::assets::AssetMetadata* script =
            authored.Assets().Manager().Registry().FindByPath(virtualPath);
        Require(script != nullptr, "kb_game degraded test behaviour asset was not discovered");
        const kb::scene::SceneObject object =
            authored.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = name });
        authored.Components().Behaviours().Set(
            object.Entity(),
            kb::scene::BehaviourComponent{
                .behaviourAssetId = script->id.value,
                .backend = kb::scene::BehaviourBackend::Lua,
            });
    };
    behaviour("Healthy", "/Game/Logic/Player.lua");
    behaviour("Broken", "/Game/Logic/Broken.lua");
    const kb::scene::SceneObject missing =
        authored.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "MissingMesh" });
    authored.Components().MeshRenderers().Set(
        missing.Entity(),
        kb::scene::MeshRendererComponent{ .meshAssetId = missingAssetId, .materialAssetId = missingAssetId + 1U });
    Require(
        kb::scene::SceneDocumentService::Save(authored, path, "Degraded"),
        "kb_game degraded test scene could not be saved");
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
    const std::wstring& arguments,
    const std::filesystem::path& logPath,
    bool startMinimized = false,
    const std::filesystem::path& executable = std::filesystem::path{ KB_GAME_EXECUTABLE_PATH }) {
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
    if (startMinimized) {
        // Windows applies this to the process's first ShowWindow call whatever
        // the program itself asked for, which is how a shortcut set to
        // "Run: minimized" reaches a game.
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWMINIMIZED;
    }
    startup.hStdInput = nullptr;
    startup.hStdOutput = log;
    startup.hStdError = log;

    // kb_game is a windowed binary, so it is started directly rather than through
    // a shell: a command interpreter does not wait for a GUI subsystem process.
    std::wstring commandLine = L"\"" + executable.wstring() + L"\" " + arguments;
    const std::filesystem::path executableDirectory = executable.parent_path();

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
    const LaunchedProcess& launched,
    const std::filesystem::path& logPath,
    DWORD timeoutMilliseconds = kProcessTimeoutMilliseconds) {
    const DWORD waited = WaitForSingleObject(launched.process, timeoutMilliseconds);
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

[[nodiscard]] ProcessRun RunGameMinimized(
    const std::wstring& arguments, const std::filesystem::path& logPath) {
    return FinishGame(LaunchGame(arguments, logPath, true), logPath);
}

// A run that has to refuse its arguments has to refuse them promptly: the
// failure pinned by these used to be an abort() behind a modal dialog, which is
// indistinguishable from a hang until the whole suite times out.
[[nodiscard]] ProcessRun RunGameExpectingRefusal(
    const std::wstring& arguments,
    const std::filesystem::path& logPath,
    const std::filesystem::path& executable = std::filesystem::path{ KB_GAME_EXECUTABLE_PATH }) {
    return FinishGame(LaunchGame(arguments, logPath, false, executable), logPath, kRefusalTimeoutMilliseconds);
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

    bool projectLoaded = false;
    for (int attempt = 0; attempt < 600 && !projectLoaded; ++attempt) {
        Require(WaitForSingleObject(launched.process, 0U) != WAIT_OBJECT_0,
            "kb_game exited before loading its project");
        std::ifstream captured{logPath, std::ios::binary};
        const std::string output{std::istreambuf_iterator<char>{captured}, std::istreambuf_iterator<char>{}};
        projectLoaded = Contains(output, " gpu_device=");
        if (!projectLoaded) Sleep(50U);
    }
    Require(projectLoaded, "kb_game did not finish loading its project before the close test");

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

// What the launch screen drew in one headless run: the first frame shows no progress, the bar only rises, the last
// frame shows a finished load, and every frame of the screen came before the first frame of the scene.
struct LoadingRun {
    std::size_t frames = 0U;
    std::size_t heldFrames = 0U;
};

[[nodiscard]] LoadingRun CheckLoadingReport(const std::filesystem::path& reportPath, const ProcessRun& run, unsigned long sceneFrames) {
    Require(run.exitCode == 0U, "kb_game did not exit successfully with its loading screen");
    const std::string json = ReadTextFile(reportPath);
    const std::vector<double> progress = ReportedNumbers(json, "progress");
    Require(progress.size() >= 3U, "kb_game drew fewer loading screen frames than the stages of the load");
    Require(progress.front() == 0.0, "the loading screen did not start with an empty bar");
    Require(progress.back() == 1.0, "the loading screen did not end with a full bar");
    for (std::size_t index = 1U; index < progress.size(); ++index) {
        Require(progress[index] >= progress[index - 1U], "the loading screen bar went backwards");
    }
    Require(std::ranges::any_of(progress, [](double value) { return value > 0.0 && value < 1.0; }),
        "the loading screen bar jumped from empty to full with no stage in between");
    const std::vector<double> beforeScene = ReportedNumbers(json, "loading_frames_before_first_scene_frame");
    Require(beforeScene.size() == 1U && static_cast<std::size_t>(beforeScene.front()) == progress.size(),
        "the scene appeared before the loading screen had drawn all of its frames");
    const std::vector<double> scene = ReportedNumbers(json, "scene_frames");
    Require(scene.size() == 1U && static_cast<unsigned long>(scene.front()) == sceneFrames,
        "kb_game did not draw its scene after the loading screen");
    std::size_t held = 0U;
    for (std::size_t at = json.find("\"held\":true"); at != std::string::npos; at = json.find("\"held\":true", at + 1U)) {
        ++held;
    }
    // The finished load draws its scene hidden behind the screen, in two frames at least, before the scene shows.
    std::size_t prewarmed = 0U;
    for (std::size_t at = json.find("\"prewarm\":true"); at != std::string::npos; at = json.find("\"prewarm\":true", at + 1U)) {
        ++prewarmed;
    }
    Require(prewarmed >= 2U, "the loading screen did not draw the loaded scene behind it before the scene showed");
    return LoadingRun{ .frames = progress.size(), .heldFrames = held };
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
    // A frame counter counts loop iterations, which a loop that draws nothing
    // and steps nothing also produces. These are the two things the frame was
    // for: the renderer accepted it, and the scene runtime was stepped by it.
    Require(
        CountReported(defaultMap.output, "rendered=") == 3UL,
        "kb_game counted frames the renderer never accepted");
    Require(
        CountReported(defaultMap.output, "ticks=") == 3UL,
        "kb_game counted frames the scene runtime was never stepped for");
    Require(
        SecondsReported(defaultMap.output, "simulated=") > 0.0,
        "kb_game stepped its scene by no time at all");

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
    Require(
        CountReported(overridden.output, "rendered=") == 2UL,
        "kb_game counted overridden-scene frames the renderer never accepted");
    Require(
        CountReported(overridden.output, "ticks=") == 2UL,
        "kb_game counted overridden-scene frames the scene runtime never ran");

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
    Require(
        CountReported(closed.output, "ticks=") == FramesReported(closed.output),
        "kb_game did not step its scene once per frame of an unbounded run");
    Require(
        CountReported(closed.output, "rendered=") > 1U,
        "kb_game submitted nothing to the renderer while its window was open");
    Require(
        SecondsReported(closed.output, "simulated=") > 0.0,
        "kb_game ran an unbounded loop that simulated no time");

    // A shortcut set to "Run: minimized" is an ordinary way to start a program.
    // The game used to refuse to start at all: its first ShowWindow is the
    // launcher's, and the renderer cannot be initialized against a zero client
    // area.
    const ProcessRun minimized =
        RunGameMinimized(projectArgument + L"--frames=3", root / "minimized.log");
    Report("minimized launch", minimized);
    Require(minimized.exitCode == 0U, "kb_game did not start when it was launched minimized");
    Require(
        Contains(minimized.output, "frames=3 shutdown=clean"),
        "kb_game launched minimized did not run the requested frames");
    Require(
        CountReported(minimized.output, "rendered=") == 3UL,
        "kb_game launched minimized rendered nothing");

    const ProcessRun missing = RunGame(
        projectArgument + L"--scene=/Game/Scenes/Absent.21kbscene --frames=1",
        root / "missing.log");
    Report("missing scene", missing);
    Require(missing.exitCode != 0U, "kb_game reported success for a scene that does not exist");
    Require(
        Contains(missing.output, "project scene asset was not found"),
        "kb_game did not name the missing scene asset");

    // Every refusal below names a path or an argument this machine's code page
    // cannot spell. Narrowing one throws, and an escaped exception ends a
    // windowed process in abort() behind a modal dialog: the branches whose only
    // job is to reject bad input used to hang the game instead. Each must
    // refuse, promptly, and say which rule was broken.
    const std::wstring unspellable = UnspellableName();
    const std::wstring quote{ L'"' };

    const ProcessRun unspellableFrames = RunGameExpectingRefusal(
        projectArgument + L"--frames=" + unspellable, root / "unspellable_frames.log");
    Report("unspellable --frames", unspellableFrames);
    Require(
        unspellableFrames.exitCode != 0U,
        "kb_game accepted a frame count that is not a number");
    Require(
        Contains(unspellableFrames.output, "--frames expects a positive frame count"),
        "kb_game did not refuse an unspellable frame count by name");

    const ProcessRun unspellableOption = RunGameExpectingRefusal(
        L"--" + unspellable + L"=1", root / "unspellable_option.log");
    Report("unspellable option", unspellableOption);
    Require(unspellableOption.exitCode != 0U, "kb_game accepted an unknown option");
    Require(
        Contains(unspellableOption.output, "unknown option"),
        "kb_game did not refuse an unspellable option by name");

    const ProcessRun unspellableProject = RunGameExpectingRefusal(
        L"--project=" + quote + (root / L"absent").wstring() + L"_" + unspellable + quote +
            L" --frames=1",
        root / "unspellable_project.log");
    Report("unspellable --project", unspellableProject);
    Require(
        unspellableProject.exitCode != 0U,
        "kb_game reported success for a project directory that does not exist");
    Require(
        Contains(unspellableProject.output, "project descriptor was not found"),
        "kb_game did not name the missing descriptor of an unspellable project path");

    const ProcessRun unspellableScene = RunGameExpectingRefusal(
        projectArgument + L"--scene=/Game/Scenes/" + unspellable + L".21kbscene --frames=1",
        root / "unspellable_scene.log");
    Report("unspellable --scene", unspellableScene);
    Require(
        unspellableScene.exitCode != 0U,
        "kb_game reported success for a scene asset that does not exist");
    Require(
        Contains(unspellableScene.output, "project scene asset was not found"),
        "kb_game did not name the missing scene asset of an unspellable reference");

    // Broken content degrades the game instead of ending it: the failing
    // behaviour is reported once with its identity and disabled, the missing
    // mesh is reported once and skipped, and every requested frame still runs.
    // The exit code reports that the run was not clean.
    WriteTextFile(root / "Assets" / "Logic" / "Broken.lua", R"(
function Tick(self, dt)
    error("deliberate script failure")
end
)");
    constexpr std::uint64_t kMissingAssetId = 0x5EED0001ULL;
    WriteDegradedScene(root, root / "Assets" / "Scenes" / "Degraded.21kbscene", kMissingAssetId);
    const ProcessRun degraded = RunGame(
        projectArgument + L"--scene=/Game/Scenes/Degraded.21kbscene --headless --frames=4",
        root / "degraded.log");
    Report("degraded content", degraded);
    Require(degraded.exitCode != 0U, "kb_game reported a clean run although its content failed");
    Require(
        Contains(degraded.output, "frames=4 shutdown=clean") &&
            CountReported(degraded.output, "rendered=") == 4UL &&
            CountReported(degraded.output, "ticks=") == 4UL,
        "kb_game stopped instead of running every frame past a failing script and a missing mesh");
    const std::size_t scriptError = degraded.output.find("deliberate script failure");
    Require(
        scriptError != std::string::npos &&
            degraded.output.find("deliberate script failure", scriptError + 1U) == std::string::npos &&
            Contains(degraded.output, "script error: 'Broken', /Game/Logic/Broken.lua: ") &&
            Contains(degraded.output, "the script was disabled after its error"),
        "kb_game did not report the failing script once, with its identity, and disable it");
    Require(
        Contains(degraded.output, "mesh=" + std::to_string(kMissingAssetId)),
        "kb_game did not name the missing mesh it skipped");

    // The loading screen: the engine's own mark first, then the project's image and a minimum display time. Each
    // headless run records the frames the screen drew and the progress each one showed.
    {
        const std::filesystem::path report = root / "loading_default.json";
        const ProcessRun plain = RunGame(
            projectArgument + L"--headless --frames=2 --loading-report=\"" + report.wstring() + L"\"",
            root / "loading_default.log");
        Report("loading screen", plain);
        const LoadingRun plainRun = CheckLoadingReport(report, plain, 2UL);
        Require(plainRun.heldFrames == 0U, "the loading screen was held although the project asks for no minimum time");

        std::filesystem::create_directories(root / "Assets" / "Branding", error);
        {
            std::ofstream image{ root / "Assets" / "Branding" / "Logo.png", std::ios::binary | std::ios::trunc };
            Require(image.is_open(), "kb_game test image could not be opened");
            image.write(reinterpret_cast<const char*>(kOnePixelPng), static_cast<std::streamsize>(sizeof(kOnePixelPng)));
            Require(image.good(), "kb_game test image could not be written");
        }
        kb::project::ProjectSettings branded = settings;
        branded.display.splash = { .image = "/Game/Branding/Logo.png", .backgroundColor = { 0.5F, 0.25F, 0.0F },
            .scale = kb::project::ProjectSplashScale::Center, .minimumDisplayMilliseconds = 400U, .showProgress = true };
        Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(root), branded, settingsError),
            "kb_game test launch screen settings could not be written");
        const std::filesystem::path brandedReport = root / "loading_branded.json";
        const ProcessRun withImage = RunGame(
            projectArgument + L"--headless --frames=2 --loading-report=\"" + brandedReport.wstring() + L"\"",
            root / "loading_branded.log");
        Report("loading screen with image", withImage);
        const LoadingRun brandedRun = CheckLoadingReport(brandedReport, withImage, 2UL);
        Require(brandedRun.heldFrames >= 1U, "the loading screen was not held for the project's minimum display time");

        branded.display.splash.image = "/Game/Branding/Missing.png";
        Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(root), branded, settingsError),
            "kb_game test launch screen settings could not be written");
        const ProcessRun missingImage = RunGame(projectArgument + L"--headless --frames=2", root / "loading_missing.log");
        Report("loading screen image missing", missingImage);
        Require(missingImage.exitCode != 0U && Contains(missingImage.output, "/Game/Branding/Missing.png"),
            "kb_game started although the launch screen image does not exist");
        Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(root), settings, settingsError),
            "kb_game test settings could not be restored");
    }

    // A scene change a script starts goes through the same screen: the script changes scene in its first tick, so the
    // startup load and the change are two runs of the bar from empty to full, both ahead of the first frame of the scene.
    {
        WriteTextFile(root / "Assets" / "Logic" / "ChangeScene.lua", R"(
local changed = false
function Tick(self, dt)
    if not changed then
        changed = true
        CallFunction("Scene.Load", { path = "/Game/Scenes/Alternate.21kbscene" })
    end
end
)");
        static_cast<void>(WriteScene(
            root, root / "Assets" / "Scenes" / "Switcher.21kbscene", "Switcher", "/Game/Logic/ChangeScene.lua", 2U));
        const std::filesystem::path report = root / "loading_change.json";
        const ProcessRun change = RunGame(
            projectArgument + L"--scene=/Game/Scenes/Switcher.21kbscene --headless --frames=4 --loading-report=\"" +
                report.wstring() + L"\"",
            root / "loading_change.log");
        Report("loading screen across a scene change", change);
        Require(change.exitCode == 0U, "kb_game did not exit successfully when a script changed scene");
        const std::string json = ReadTextFile(report);
        const std::vector<double> progress = ReportedNumbers(json, "progress");
        const std::vector<double> beforeScene = ReportedNumbers(json, "loading_frames_before_first_scene_frame");
        Require(beforeScene.size() == 1U && static_cast<std::size_t>(beforeScene.front()) == progress.size(),
            "the scene appeared before the loading screen had drawn the frames of a scene change");
        // The startup load ends with its full-bar frames (the scene drawn behind the screen).
        auto startupEnd = std::ranges::find(progress, 1.0);
        while (startupEnd != progress.end() && std::next(startupEnd) != progress.end() && *std::next(startupEnd) == 1.0) {
            ++startupEnd;
        }
        Require(startupEnd != progress.end() && std::next(startupEnd) != progress.end(),
            "the loading screen did not come back when a script changed scene");
        const std::size_t startup = static_cast<std::size_t>(startupEnd - progress.begin()) + 1U;
        Require(progress[startup] == 0.0 && progress.back() == 1.0 && progress.size() - startup >= 3U,
            "a scene change did not run the loading screen from empty to full");
        for (std::size_t index = startup + 1U; index < progress.size(); ++index) {
            Require(progress[index] >= progress[index - 1U], "the loading screen bar went backwards during a scene change");
        }
        std::size_t prewarmed = 0U;
        for (std::size_t at = json.find("\"prewarm\":true"); at != std::string::npos; at = json.find("\"prewarm\":true", at + 1U)) {
            ++prewarmed;
        }
        Require(prewarmed >= 4U, "a scene change did not draw the new scene behind the loading screen before it showed");
        Require(Contains(change.output, "frames=4 shutdown=clean"), "kb_game did not keep running after a script changed scene");
    }

    // A player staged beside its own Game.kbpack is a shipped game: it runs
    // only that package, refuses every development switch by name, and never
    // falls back to a loose project when its package cannot be mounted.
    const std::filesystem::path shippedRoot = root / "Shipped";
    std::filesystem::create_directories(shippedRoot, error);
    const std::filesystem::path shippedPlayer = shippedRoot / "ShippedGame.exe";
    std::filesystem::copy_file(KB_GAME_EXECUTABLE_PATH, shippedPlayer, std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "kb_game shipped-player copy could not be staged");
    WriteTextFile(shippedRoot / "Game.kbpack", "not a cooked package");
    const ProcessRun shippedProject = RunGameExpectingRefusal(
        projectArgument + L"--headless --frames=1", root / "shipped_project.log", shippedPlayer);
    Report("shipped --project", shippedProject);
    Require(
        shippedProject.exitCode != 0U &&
            Contains(shippedProject.output, "--project is a development option") &&
            !Contains(shippedProject.output, "entities="),
        "A shipped game accepted --project instead of refusing it by name");
    const ProcessRun shippedProfile = RunGameExpectingRefusal(
        L"--headless --frames=1 --profile-file=\"" + (root / "overwritten.csv").wstring() + L"\"",
        root / "shipped_profile.log",
        shippedPlayer);
    Report("shipped --profile-file", shippedProfile);
    Require(
        shippedProfile.exitCode != 0U &&
            Contains(shippedProfile.output, "--profile-file is a development option") &&
            !std::filesystem::exists(root / "overwritten.csv"),
        "A shipped game accepted a development output switch");
    const ProcessRun shippedCorrupt = RunGameExpectingRefusal(
        L"--headless --frames=1", root / "shipped_corrupt.log", shippedPlayer);
    Report("shipped corrupt package", shippedCorrupt);
    Require(
        shippedCorrupt.exitCode != 0U &&
            Contains(shippedCorrupt.output, "runtime package could not be mounted") &&
            !Contains(shippedCorrupt.output, "entities="),
        "A shipped game with an unreadable package did not stop with a package error");

    std::fputs("kb_game launch tests passed\n", stdout);
    return EXIT_SUCCESS;
}
