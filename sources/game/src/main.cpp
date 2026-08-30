#include "GameProjectRuntime.hpp"
#include "GameWindow.hpp"

#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// A frame that took longer than this is stepped as if it had taken exactly this
// long, so a stall cannot tunnel a moving body through the world.
constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;
constexpr std::uint32_t kDefaultWindowWidth = 1280U;
constexpr std::uint32_t kDefaultWindowHeight = 720U;

struct GameOptions {
    std::filesystem::path projectPath;
    std::string sceneOverride;
    // 0 runs until the player closes the window; a positive value bounds the
    // run so an automated check can drive the real executable to completion.
    std::uint32_t frameLimit = 0U;
};

[[nodiscard]] bool HasPrefix(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] bool ParseArguments(int argc, wchar_t** argv, GameOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{ argv[index] };
        if (HasPrefix(argument, L"--project=")) {
            options.projectPath =
                std::filesystem::path{ std::wstring{ argument.substr(10U) } };
        } else if (HasPrefix(argument, L"--scene=")) {
            options.sceneOverride =
                std::filesystem::path{ std::wstring{ argument.substr(8U) } }.generic_string();
        } else if (HasPrefix(argument, L"--frames=")) {
            const std::string value =
                std::filesystem::path{ std::wstring{ argument.substr(9U) } }.string();
            const char* begin = value.data();
            const char* end = begin + value.size();
            std::uint32_t frames = 0U;
            const std::from_chars_result parsed = std::from_chars(begin, end, frames);
            if (parsed.ec != std::errc{} || parsed.ptr != end || frames == 0U) {
                std::cerr << "kb_game: --frames expects a positive frame count\n";
                return false;
            }
            options.frameLimit = frames;
        } else {
            std::cerr << "kb_game: unknown option '"
                      << std::filesystem::path{ std::wstring{ argument } }.string() << "'\n";
            return false;
        }
    }
    return true;
}

[[nodiscard]] float RuntimeDeltaSeconds(
    std::chrono::steady_clock::time_point previous,
    std::chrono::steady_clock::time_point current) noexcept {
    const std::chrono::duration<float> delta = current - previous;
    return std::clamp(delta.count(), 0.0F, kMaximumRuntimeDeltaSeconds);
}

[[nodiscard]] std::wstring WindowTitle(const kb::game::GameProjectRuntime& runtime) {
    if (runtime.gameName.empty()) {
        return L"21kb Game";
    }
    return std::filesystem::path{ runtime.gameName }.wstring();
}

int RunGame(const GameOptions& options) {
    kb::game::GameProjectRuntime projectRuntime{};
    if (!ReadGameProjectRuntime(
            options.projectPath, options.sceneOverride, projectRuntime, std::cerr)) {
        return EXIT_FAILURE;
    }

    kb::input::Win32InputCollector inputCollector;
    kb::game::GameWindow window;
    if (!window.Open(
            WindowTitle(projectRuntime),
            kDefaultWindowWidth,
            kDefaultWindowHeight,
            inputCollector)) {
        std::cerr << "kb_game: game window could not be created\n";
        return EXIT_FAILURE;
    }

    auto scriptModuleOwner = std::make_unique<kb::script::ScriptModule>();
    kb::script::ScriptModule* scriptModule = scriptModuleOwner.get();
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules;
    staticModules.push_back(std::move(scriptModuleOwner));

    kb::scene::Scene scene{ std::move(projectRuntime.descriptor), std::move(staticModules) };
    const bool scriptActive = scene.IsModuleActive("Script");
    if (scriptActive && (!scriptModule->Succeeded() || scriptModule->Host() == nullptr)) {
        std::cerr << "kb_game: script module initialization failed\n";
        for (const std::string& diagnostic : scriptModule->Diagnostics()) {
            std::cerr << "kb_game: script module diagnostic: " << diagnostic << '\n';
        }
        return EXIT_FAILURE;
    }

    std::filesystem::path scenePath;
    std::size_t discoveredAssets = 0U;
    if (!LoadGameProjectScene(projectRuntime, scene, scenePath, discoveredAssets, std::cerr)) {
        return EXIT_FAILURE;
    }
    std::cout << "kb_game: project=" << projectRuntime.projectRoot.string()
              << " scene=" << scenePath.string()
              << " entities=" << scene.Entities().Count()
              << " assets=" << discoveredAssets
              << " modules=" << scene.ActiveModuleCount() << '\n';
    std::cout.flush();

    kb::render::DisplayConfig displayConfig{};
    kb::render::Renderer renderer;
    renderer.SetGraphShaderCacheRoot(
        (kb::game::ExecutableDirectory() / ".cache" / "graph_shaders").generic_string());
    if (!renderer.Initialize(window, &displayConfig)) {
        std::cerr << "kb_game: renderer initialization failed\n";
        return EXIT_FAILURE;
    }

    kb::input::Win32XInputHapticsBackend hapticsBackend;
    kb::input::InputHaptics::RegisterBackend(scene, hapticsBackend);

    std::uint32_t renderedFrames = 0U;
    auto previousTick = std::chrono::steady_clock::now();
    while (window.PumpMessages() && !scene.Runtime().ShouldQuit()) {
        if (window.Width() == 0U || window.Height() == 0U) {
            // Minimized: there is nothing to draw and nothing to time against,
            // so block on the queue instead of spinning.
            static_cast<void>(WaitMessage());
            previousTick = std::chrono::steady_clock::now();
            continue;
        }
        std::uint32_t resizedWidth = 0U;
        std::uint32_t resizedHeight = 0U;
        if (window.ConsumeResize(resizedWidth, resizedHeight)) {
            renderer.OnResize(resizedWidth, resizedHeight);
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = RuntimeDeltaSeconds(previousTick, now);
        previousTick = now;

        inputCollector.Collect(scene.Input().MutableDeviceState(), window.Handle());
        static_cast<void>(scene.Runtime().Update(deltaSeconds));

        if (renderer.BeginFrame()) {
            renderer.SubmitScene(scene);
            renderer.EndFrame();
        }
        ++renderedFrames;
        if (options.frameLimit != 0U && renderedFrames >= options.frameLimit) {
            break;
        }
    }

    if (scriptActive && !scriptModule->Host()->DispatchShutdownLifecycle(0.0F)) {
        std::cerr << "kb_game: script shutdown lifecycle could not be dispatched\n";
    }
    hapticsBackend.StopAll();
    kb::input::InputHaptics::UnregisterBackend(scene, hapticsBackend);
    renderer.ReleaseScene(scene);
    renderer.Shutdown();

    std::cout << "kb_game: frames=" << renderedFrames << " shutdown=clean\n";
    std::cout.flush();
    return EXIT_SUCCESS;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    GameOptions options{};
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        std::cerr << "kb_game: command line could not be read\n";
        return EXIT_FAILURE;
    }
    const bool parsed = ParseArguments(argc, argv, options);
    LocalFree(argv);
    if (!parsed) {
        return EXIT_FAILURE;
    }
    // A packaged game keeps its project beside the executable, so an argument-less
    // launch starts the project's own ProjectSettings::defaultMap.
    if (options.projectPath.empty()) {
        options.projectPath = kb::game::ExecutableDirectory();
    }
    return RunGame(options);
}
