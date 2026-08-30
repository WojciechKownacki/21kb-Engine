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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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

// Read straight off the wide argument. Converting it to a narrow string first
// would throw for an argument this machine's code page cannot spell, which turns
// the branch whose whole job is to reject bad input into an abort().
[[nodiscard]] bool ParseFrameLimit(std::wstring_view text, std::uint32_t& frames) noexcept {
    if (text.empty() || text.size() > 10U) {
        return false;
    }
    std::uint64_t value = 0U;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint64_t>(character - L'0');
        if (value > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (value == 0U) {
        return false;
    }
    frames = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool ParseArguments(int argc, wchar_t** argv, GameOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{ argv[index] };
        if (HasPrefix(argument, L"--project=")) {
            options.projectPath =
                std::filesystem::path{ std::wstring{ argument.substr(10U) } };
        } else if (HasPrefix(argument, L"--scene=")) {
            std::wstring reference{ argument.substr(8U) };
            std::replace(reference.begin(), reference.end(), L'\\', L'/');
            options.sceneOverride =
                kb::game::NarrowForDiagnostics(std::wstring_view{ reference });
        } else if (HasPrefix(argument, L"--frames=")) {
            std::uint32_t frames = 0U;
            if (!ParseFrameLimit(argument.substr(9U), frames)) {
                std::cerr << "kb_game: --frames expects a positive frame count\n";
                return false;
            }
            options.frameLimit = frames;
        } else {
            std::cerr << "kb_game: unknown option '"
                      << kb::game::NarrowForDiagnostics(argument) << "'\n";
            return false;
        }
    }
    return true;
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
    std::cout << "kb_game: project=" << kb::game::NarrowForDiagnostics(projectRuntime.projectRoot)
              << " scene=" << kb::game::NarrowForDiagnostics(scenePath)
              << " entities=" << scene.Entities().Count()
              << " assets=" << discoveredAssets
              << " modules=" << scene.ActiveModuleCount() << '\n';
    std::cout.flush();

    kb::render::DisplayConfig displayConfig{};
    kb::render::Renderer renderer;
    // A cache directory whose name cannot be spelled exactly in this machine's
    // code page is worse than no cache directory: the renderer would read and
    // write somewhere else entirely. The game runs without the cache instead.
    const std::filesystem::path shaderCacheRoot =
        kb::game::ExecutableDirectory() / ".cache" / "graph_shaders";
    if (std::optional<std::string> cacheRoot =
            kb::game::TryNarrow(shaderCacheRoot.generic_wstring());
        cacheRoot.has_value()) {
        renderer.SetGraphShaderCacheRoot(*std::move(cacheRoot));
    } else {
        std::cerr << "kb_game: graph shader cache disabled: "
                  << kb::game::NarrowForDiagnostics(shaderCacheRoot)
                  << " cannot be named in this system's code page\n";
    }
    if (!renderer.Initialize(window, &displayConfig)) {
        std::cerr << "kb_game: renderer initialization failed\n";
        return EXIT_FAILURE;
    }

    kb::input::Win32XInputHapticsBackend hapticsBackend;
    kb::input::InputHaptics::RegisterBackend(scene, hapticsBackend);

    std::uint32_t renderedFrames = 0U;
    std::uint32_t submittedFrames = 0U;
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
        const float deltaSeconds = kb::game::RuntimeDeltaSeconds(previousTick, now);
        previousTick = now;

        inputCollector.Collect(scene.Input().MutableDeviceState(), window.Handle());
        static_cast<void>(scene.Runtime().Update(deltaSeconds));

        if (renderer.BeginFrame()) {
            renderer.SubmitScene(scene);
            renderer.EndFrame();
            ++submittedFrames;
        }
        ++renderedFrames;
        if (options.frameLimit != 0U && renderedFrames >= options.frameLimit) {
            break;
        }
    }

    // "clean" is a claim about what ran, so a lifecycle that could not be
    // dispatched has to cost the exit code too: a game that reports success
    // after failing to shut its scripts down is exactly the report nobody can
    // act on.
    bool shutdownClean = true;
    if (scriptActive && !scriptModule->Host()->DispatchShutdownLifecycle(0.0F)) {
        std::cerr << "kb_game: script shutdown lifecycle could not be dispatched\n";
        shutdownClean = false;
    }
    hapticsBackend.StopAll();
    kb::input::InputHaptics::UnregisterBackend(scene, hapticsBackend);
    renderer.ReleaseScene(scene);
    renderer.Shutdown();

    // frames counts loop iterations; rendered counts the ones the renderer
    // actually accepted, ticks the ones the scene runtime actually stepped and
    // simulated the time it was stepped by. Reporting only the first would let a
    // loop that draws nothing and simulates nothing look identical to one that
    // does both.
    std::cout << "kb_game: frames=" << renderedFrames
              << " shutdown=" << (shutdownClean ? "clean" : "incomplete")
              << " rendered=" << submittedFrames
              << " ticks=" << scene.Runtime().FrameIndex()
              << " simulated=" << scene.Runtime().ElapsedSeconds() << '\n';
    std::cout.flush();
    return shutdownClean ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Nothing below may let an exception escape: this is a windowed process, so
    // an escaped exception ends in abort() behind a modal dialog that no player
    // and no automated run can dismiss.
    try {
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
        // A packaged game keeps its project beside the executable, so an
        // argument-less launch starts the project's own ProjectSettings::defaultMap.
        if (options.projectPath.empty()) {
            options.projectPath = kb::game::ExecutableDirectory();
        }
        return RunGame(options);
    } catch (const std::exception& error) {
        std::cerr << "kb_game: unrecoverable error: " << error.what() << '\n';
        std::cerr.flush();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "kb_game: unrecoverable error\n";
        std::cerr.flush();
        return EXIT_FAILURE;
    }
}
