#include "GameProjectRuntime.hpp"
#include "GameWindow.hpp"
#include "RuntimeSceneFrameSync.hpp"
#include "ParticleRuntimeDiagnostics.hpp"
#include "GameScriptLog.hpp"
#include "GameRuntimeDiagnostics.hpp"
#include "kb/net/SceneClient.hpp"

#include "engine/input/InputHaptics.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/input/InputRecording.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/platform/CrashReporting.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneCrashReportConsent.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RendererMemory.hpp"
#include "kb/render/scene/SceneLightingQuality.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"

#include <bgfx/bgfx.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultWindowWidth = 1280U;
constexpr std::uint32_t kDefaultWindowHeight = 720U;

struct GameOptions {
    std::filesystem::path projectPath;
    std::filesystem::path networkConfigPath;
    std::string sceneOverride;
    std::uint32_t width = kDefaultWindowWidth;
    std::uint32_t height = kDefaultWindowHeight;
    bool fullscreen = false;
    bool headless = false;
    std::filesystem::path luaDebugDirectory;
    bool uncapped = false;
    bool profileFixedStep = false;
    bool profileSceneOverdraw = false;
    bool particleDiagnostics = false;
    bool particleRequireGpu = false;
    std::uint32_t particleSamples = 64U;
    std::filesystem::path profilePath;
    std::filesystem::path inputReplayPath;
    std::optional<kb::render::SceneRenderLightingPath> profileLighting;
    // -1 lets the renderer choose; otherwise a bgfx::RendererType.
    std::int32_t rendererType = -1;
    std::filesystem::path screenshotPath;
    std::uint32_t screenshotFrame = 0U;
    // 0 runs until the player closes the window; a positive value bounds the
    // run so an automated check can drive the real executable to completion.
    std::uint32_t frameLimit = 0U;
    bool help = false;
};

constexpr std::string_view kUsage = R"(kb_game - runs a 21kb project or packaged game

Usage: kb_game [options]

  --project=<dir|Name.21kbproject>  project to run (default: the game beside this executable)
  --scene=/Game/<path>.21kbscene    start scene (default: the project's startup map)
  --network-config=<file.json>     connect using runtime UDP session settings
  --width=<64..8192> --height=<64..8192>  window size
  --fullscreen                      primary monitor, native resolution
  --headless                        render without showing the window (needs --frames)
  --lua-debug-dir=<new-directory>   enable the local Lua debugger endpoint
  --frames=<n>                      stop after n frames
  --particle-diagnostics            terminal particle JSON; fixed 1/60 step, uncapped
                                    (needs --headless --frames=1..36000)
  --particle-samples=<1..4096>       maximum captured ring slots per emitter (default 64)
  --particle-require-gpu            fail diagnostic runs on fallback or missing GPU proof
  --uncapped                        no frame rate limit
  --screenshot-file=<file.png>      write frame --screenshot-frame as PNG (the file must not exist)
  --screenshot-frame=<n>            which frame to capture (within --frames)
  --profile-file=<file>             per-frame timings (needs --frames=1..120000)
  --input-replay=<file>             replay recorded input and time (needs --headless --frames)
  --profile-fixed-step              fixed time step while profiling (needs --headless)
  --profile-scene-overdraw           measure scene geometry and particle overdraw (needs --profile-file)
  --profile-lighting=Forward|ForwardPlus|Deferred
  --renderer=d3d11|d3d12|vulkan|opengl  graphics backend (default: the platform's)
  --help                            this text
)";

struct GameFrameProfile {
    double wallFrameMilliseconds = 0.0;
    double cpuMilliseconds = 0.0;
    double simulationMilliseconds = 0.0;
    double renderMilliseconds = 0.0;
    double beginSubmitMilliseconds = 0.0;
    double endFrameMilliseconds = 0.0;
    double gpuDelayedMilliseconds = -1.0;
    double bgfxWaitRenderMilliseconds = -1.0;
    double bgfxWaitSubmitMilliseconds = -1.0;
    std::uint32_t draws = 0U;
    std::uint32_t shadowCasters = 0U;
    std::uint32_t submittedMeshes = 0U;
    std::uint32_t droppedInstances = 0U;
    std::uint32_t missingResources = 0U;
    std::uint32_t fixedSteps = 0U;
    std::uint32_t exposureReadbackSubmitted = 0U;
    std::uint32_t exposureSampleAvailable = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t visibleMeshes = 0U;
    std::uint32_t culledInstances = 0U;
    std::uint32_t sceneLights = 0U;
    std::uint32_t submittedLights = 0U;
    std::uint32_t skippedLights = 0U;
    std::uint32_t lightCapacity = 0U;
    std::uint32_t lightingPath = 0U;
    std::uint32_t shadowDraws = 0U;
    std::uint64_t instanceUploadBytes = 0U;
    double transformMilliseconds = 0.0;
    double sceneSyncMilliseconds = 0.0;
    std::uint32_t transformInspected = 0U;
    std::uint32_t transformUpdated = 0U;
    std::uint32_t meshCommandReuseCount = 0U;
    std::uint32_t sceneEntities = 0U;
    std::uint32_t streamingOperations = 0U;
    double streamingMilliseconds = 0.0;
    float renderCameraX = 0.0F;
    float renderCameraY = 0.0F;
    float renderCameraZ = 0.0F;
    bool renderCameraValid = false;
    kb::world::WorldMemoryReport worldMemory{};
    double sceneUpdateMilliseconds = 0.0;
    double fixedSimulationMilliseconds = 0.0;
    double ecsSystemsMilliseconds = 0.0;
};

std::ostream& WriteWorldMemoryProfile(std::ostream& output, const kb::world::WorldMemoryReport& report) {
    output << ',' << report.budgetBytes << ',' << report.estimatedResidentBytes << ',' << report.residentUnits
           << ',' << report.measuredBytes << ',' << report.estimatedBytes << ',' << report.errorRatio;
    for (const auto& category : report.categories) {
        output << ',' << static_cast<unsigned>(category.source) << ',';
        if (category.source != kb::world::WorldMemorySource::Unavailable) output << category.bytes;
        output << ',';
        if (category.source != kb::world::WorldMemorySource::Unavailable) output << category.baselineBytes;
    }
    const auto& assets = report.categories[static_cast<std::size_t>(kb::world::WorldMemoryKind::AssetsCpu)];
    return output << ',' << assets.knownPayloadBytes << ',' << assets.unmeasuredPayloads << ',' << assets.baselineUnmeasuredPayloads;
}

[[nodiscard]] bool HasPrefix(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
}

// Switches that load content other than the game's own package, pick a scene
// the game did not start in, or write files where the command line says. Only
// a development player accepts them; a shipped game refuses them.
constexpr std::array<std::wstring_view, 10U> kDevelopmentSwitches{
    L"--project=",
    L"--scene=",
    L"--profile-fixed-step",
    L"--profile-scene-overdraw",
    L"--profile-lighting=",
    L"--profile-file=",
    L"--screenshot-file=",
    L"--screenshot-frame=",
    L"--renderer=",
    L"--input-replay=",
};

// Names the first development switch on the command line, or returns nothing.
[[nodiscard]] std::wstring_view FirstDevelopmentSwitch(int argc, wchar_t** argv) noexcept {
    for (int index = 1; index < argc; ++index) {
        for (const std::wstring_view option : kDevelopmentSwitches) {
            if (HasPrefix(argv[index], option)) {
                return option.substr(0U, option.find(L'='));
            }
        }
    }
    return {};
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

[[nodiscard]] bool ParseWindowExtent(std::wstring_view text, std::uint32_t& extent) noexcept {
    if (!ParseFrameLimit(text, extent) || extent < 64U || extent > 8192U) {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseArguments(int argc, wchar_t** argv, GameOptions& options) {
    bool explicitWidth = false;
    bool explicitHeight = false;
    bool particleSamplesSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{ argv[index] };
        if (argument == L"--help" || argument == L"-h") {
            std::cout << kUsage;
            options.help = true;
            return true;
        }
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
        } else if (HasPrefix(argument, L"--network-config=")) {
            if (!options.networkConfigPath.empty() || argument.size() == 17U) {
                std::cerr << "kb_game: --network-config expects one settings file\n";
                return false;
            }
            options.networkConfigPath = std::wstring{argument.substr(17U)};
        } else if (HasPrefix(argument, L"--width=")) {
            if (!ParseWindowExtent(argument.substr(8U), options.width)) {
                std::cerr << "kb_game: --width expects 64..8192 pixels\n";
                return false;
            }
            explicitWidth = true;
        } else if (HasPrefix(argument, L"--height=")) {
            if (!ParseWindowExtent(argument.substr(9U), options.height)) {
                std::cerr << "kb_game: --height expects 64..8192 pixels\n";
                return false;
            }
            explicitHeight = true;
        } else if (argument == L"--fullscreen") {
            options.fullscreen = true;
        } else if (argument == L"--uncapped") {
            options.uncapped = true;
        } else if (HasPrefix(argument, L"--lua-debug-dir=")) {
            options.luaDebugDirectory = std::wstring{argument.substr(16U)};
            if (options.luaDebugDirectory.empty()) return false;
        } else if (argument == L"--headless") {
            options.headless = true;
        } else if (argument == L"--particle-diagnostics") {
            options.particleDiagnostics = true;
        } else if (argument == L"--particle-require-gpu") {
            options.particleRequireGpu = true;
        } else if (HasPrefix(argument, L"--particle-samples=")) {
            particleSamplesSpecified = true;
            if (!ParseFrameLimit(argument.substr(19U), options.particleSamples) || options.particleSamples > 4096U) {
                std::cerr << "kb_game: --particle-samples expects 1..4096\n";
                return false;
            }
        } else if (argument == L"--profile-fixed-step") {
            options.profileFixedStep = true;
        } else if (argument == L"--profile-scene-overdraw") {
            options.profileSceneOverdraw = true;
        } else if (HasPrefix(argument, L"--screenshot-file=")) {
            if (argument.size() == 18U) {
                std::cerr << "kb_game: --screenshot-file requires a path\n";
                return false;
            }
            options.screenshotPath = std::filesystem::path{std::wstring{argument.substr(18U)}};
        } else if (HasPrefix(argument, L"--screenshot-frame=")) {
            if (!ParseFrameLimit(argument.substr(19U), options.screenshotFrame)) {
                std::cerr << "kb_game: --screenshot-frame expects a positive frame count\n";
                return false;
            }
        } else if (HasPrefix(argument, L"--renderer=")) {
            const auto value = argument.substr(11U);
            if (value == L"d3d11") options.rendererType = bgfx::RendererType::Direct3D11;
            else if (value == L"d3d12") options.rendererType = bgfx::RendererType::Direct3D12;
            else if (value == L"vulkan") options.rendererType = bgfx::RendererType::Vulkan;
            else if (value == L"opengl") options.rendererType = bgfx::RendererType::OpenGL;
            else {
                std::cerr << "kb_game: --renderer expects d3d11, d3d12, vulkan or opengl\n";
                return false;
            }
        } else if (HasPrefix(argument, L"--profile-lighting=")) {
            const auto value = argument.substr(19U);
            if (value == L"Forward") options.profileLighting = kb::render::SceneRenderLightingPath::Forward;
            else if (value == L"ForwardPlus") options.profileLighting = kb::render::SceneRenderLightingPath::ClusteredForwardPlus;
            else if (value == L"Deferred") options.profileLighting = kb::render::SceneRenderLightingPath::Deferred;
            else {
                std::cerr << "kb_game: --profile-lighting expects Forward, ForwardPlus or Deferred\n";
                return false;
            }
        } else if (HasPrefix(argument, L"--input-replay=")) {
            if (argument.size() == 15U) {
                std::cerr << "kb_game: --input-replay requires a path\n";
                return false;
            }
            options.inputReplayPath = std::filesystem::path{std::wstring{argument.substr(15U)}};
        } else if (HasPrefix(argument, L"--profile-file=")) {
            if (argument.size() == 15U) {
                std::cerr << "kb_game: --profile-file requires a path\n";
                return false;
            }
            options.profilePath = std::filesystem::path{std::wstring{argument.substr(15U)}};
        } else {
            std::cerr << "kb_game: unknown option '"
                      << kb::game::NarrowForDiagnostics(argument) << "' (kb_game --help lists the options)\n";
            return false;
        }
    }
    if (options.headless && options.fullscreen) {
        std::cerr << "kb_game: --headless cannot use --fullscreen\n";
        return false;
    }
    if ((options.particleDiagnostics && (!options.headless || options.frameLimit == 0U || options.frameLimit > 36000U)) ||
        (!options.particleDiagnostics && (options.particleRequireGpu || particleSamplesSpecified))) {
        std::cerr << "kb_game: particle diagnostics require --particle-diagnostics --headless --frames=1..36000\n";
        return false;
    }
    if (options.profileFixedStep && (!options.headless || options.profilePath.empty())) {
        std::cerr << "kb_game: --profile-fixed-step requires --headless and --profile-file\n";
        return false;
    }
    if (options.profileSceneOverdraw && options.profilePath.empty()) {
        std::cerr << "kb_game: --profile-scene-overdraw requires --profile-file\n";
        return false;
    }
    if (!options.inputReplayPath.empty() && (!options.headless || options.frameLimit == 0U ||
        options.profileFixedStep || options.particleDiagnostics)) {
        std::cerr << "kb_game: input replay requires --headless --frames and uses recorded timing\n";
        return false;
    }
    if (options.fullscreen && (explicitWidth || explicitHeight)) {
        std::cerr << "kb_game: --fullscreen uses the primary monitor's native resolution; omit --width and --height\n";
        return false;
    }
    if (!options.profilePath.empty() &&
        (options.frameLimit == 0U || options.frameLimit > 120'000U)) {
        std::cerr << "kb_game: --profile-file requires --frames=1..120000\n";
        return false;
    }
    if (options.screenshotPath.empty() != (options.screenshotFrame == 0U) ||
        (options.screenshotFrame != 0U &&
            (options.frameLimit == 0U || options.screenshotFrame > options.frameLimit))) {
        std::cerr << "kb_game: screenshot requires --screenshot-file, --screenshot-frame and --frames covering that frame\n";
        return false;
    }
    if (!options.screenshotPath.empty() &&
        (std::filesystem::exists(options.screenshotPath) ||
            std::filesystem::exists(options.screenshotPath.string() + ".pending"))) {
        std::cerr << "kb_game: screenshot destination must not already exist\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::wstring WindowTitle(const kb::game::GameProjectRuntime& runtime) {
    if (runtime.gameName.empty()) {
        return L"21kb Game";
    }
    return std::filesystem::path{ runtime.gameName }.wstring();
}

// Where scripts persist saves and settings. A loose project keeps them beside
// itself, like the editor's play mode; a packaged game may be installed where
// the player cannot write, so it uses its own directory under the per-user
// local application data folder, named after the game.
[[nodiscard]] std::filesystem::path GameUserStorageRoot(const kb::game::GameProjectRuntime& runtime) {
    if (!runtime.IsPackaged()) {
        return runtime.projectRoot / "Saves";
    }
    std::string directory = runtime.gameName.substr(0U, kb::platform::kMaxUserStorageSlotNameBytes);
    for (char& character : directory) {
        if (!kb::platform::IsUserStorageSlotName(std::string_view{ &character, 1U })) {
            character = '_';
        }
    }
    if (!kb::platform::IsUserStorageSlotName(directory)) {
        directory = "21kbGame";
    }
    PWSTR localData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localData)) || localData == nullptr) {
        CoTaskMemFree(localData);
        return {};
    }
    std::filesystem::path root{ localData };
    CoTaskMemFree(localData);
    return root / directory / "Saves";
}

int RunGame(const GameOptions& options) {
    std::optional<kb::net::SceneClientOptions> networkOptions;
    if (!options.networkConfigPath.empty()) {
        std::ifstream input(options.networkConfigPath, std::ios::binary);
        std::array<char, 8193U> content{};
        if (!input) {
            std::cerr << "kb_game: network settings file could not be opened\n";
            return EXIT_FAILURE;
        }
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        kb::net::SceneClientOptions parsed;
        if (input.bad() || !kb::net::ParseNativeSceneClientSettings(
                std::string_view{content.data(), static_cast<std::size_t>(input.gcount())}, parsed)) {
            std::cerr << "kb_game: invalid network settings\n";
            return EXIT_FAILURE;
        }
        networkOptions = std::move(parsed);
    }
    kb::input::InputRecording inputReplay;
    if (!options.inputReplayPath.empty()) {
        auto loaded = kb::input::ReadInputRecording(options.inputReplayPath);
        if (!loaded.succeeded || loaded.asset.empty() || options.frameLimit > loaded.asset.size()) {
            std::cerr << "kb_game: input replay: " << (loaded.succeeded
                ? "recording must contain every requested frame" : loaded.error) << '\n';
            return EXIT_FAILURE;
        }
        inputReplay = std::move(loaded.asset);
    }
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == 0 &&
        AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == 0) {
        std::cerr << "kb_game: per-monitor DPI awareness is required\n";
        return EXIT_FAILURE;
    }
    kb::game::GameProjectRuntime projectRuntime{};
    if (!ReadGameProjectRuntime(
            options.projectPath, options.sceneOverride, projectRuntime, std::cerr)) {
        return EXIT_FAILURE;
    }

    kb::input::Win32InputCollector inputCollector;
    kb::game::GameWindow window;
    if (!window.Open(
            WindowTitle(projectRuntime),
            options.width,
            options.height,
            options.fullscreen,
            inputCollector,
            !options.headless)) {
        std::cerr << "kb_game: game window could not be created\n";
        return EXIT_FAILURE;
    }

    kb::render::DisplayConfig displayConfig{};
    displayConfig.enableEditorRendering = false;
    displayConfig.preferredBgfxRendererType = options.rendererType;
    if (options.uncapped || options.particleDiagnostics) {
        displayConfig.syncMode = kb::render::DisplaySyncMode::Uncapped;
    }
    kb::render::Renderer renderer;
    if (projectRuntime.IsPackaged()) {
        std::string providerError;
        const std::shared_ptr<kb::render::RuntimeAssetShaderProvider> provider =
            kb::render::RuntimeAssetShaderProvider::Create(projectRuntime.assetPack, providerError);
        if (provider == nullptr || !renderer.SetShaderBinaryProvider(provider)) {
            std::cerr << "kb_game: packaged shader provider could not be configured: "
                      << providerError << '\n';
            return EXIT_FAILURE;
        }
    } else {
        // The project's own graph shader cache, the one the editor cooks into
        // (EditorSceneContext), so a project run outside the editor draws its
        // graph materials instead of the built-in fallback. A cache directory
        // whose name cannot be spelled exactly in this machine's code page is
        // worse than no cache directory.
        const std::filesystem::path shaderCacheRoot =
            projectRuntime.projectRoot / ".cache" / "graph_shaders";
        if (std::optional<std::string> cacheRoot =
                kb::game::TryNarrow(shaderCacheRoot.generic_wstring());
            cacheRoot.has_value()) {
            renderer.SetGraphShaderCacheRoot(*std::move(cacheRoot));
            // A graph material the editor has not cooked yet is cooked when first drawn.
            const kb::render::RenderMaterialGraphShaderToolchain toolchain =
                kb::render::ResolveRenderMaterialGraphShaderToolchain({});
            if (toolchain.Available()) {
                renderer.SetGraphShaderCookToolchain(toolchain);
            } else {
                std::cerr << "kb_game: graph materials not cooked by the editor draw with the built-in "
                             "material: bgfx shaderc or the engine shader sources were not found (set "
                             "KB_GRAPH_SHADERC to shaderc, or build the shaderc target)\n";
            }
        } else {
            std::cerr << "kb_game: graph shader cache disabled: "
                      << kb::game::NarrowForDiagnostics(shaderCacheRoot)
                      << " cannot be named in this system's code page\n";
        }
    }
    if (!renderer.Initialize(window, &displayConfig)) {
        std::cerr << "kb_game: renderer initialization failed\n";
        return EXIT_FAILURE;
    }
    {
        auto lighting = renderer.DefaultSceneLightingConfig();
        switch (projectRuntime.lightingPath) {
        case kb::project::ProjectSceneLightingPath::Forward: lighting.lightingPath = kb::render::SceneRenderLightingPath::Forward; break;
        case kb::project::ProjectSceneLightingPath::ForwardPlus: lighting.lightingPath = kb::render::SceneRenderLightingPath::ClusteredForwardPlus; break;
        case kb::project::ProjectSceneLightingPath::Deferred: lighting.lightingPath = kb::render::SceneRenderLightingPath::Deferred; break;
        }
        if (options.profileLighting) lighting.lightingPath = *options.profileLighting;
        kb::render::ApplyProjectLightingQuality(lighting, projectRuntime.shadowQuality, projectRuntime.giQuality, kb::game::kMobileDevice);
        lighting.maxForwardLights = lighting.lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus
            ? kb::render::kMaxSceneForwardPlusLights : kb::render::kMaxSceneForwardLights;
        renderer.SetDefaultSceneLightingConfig(lighting);
    }

    const std::filesystem::path userStorageRoot = GameUserStorageRoot(projectRuntime);
    kb::script::ScriptModuleOptions scriptOptions;
    scriptOptions.runtimeOptions.userStorageRoot = userStorageRoot;
    scriptOptions.runtimeOptions.disableFailingBehaviours = true;
    scriptOptions.configureHost = &kb::game::ConfigureGameScriptLog;
    auto scriptModuleOwner = std::make_unique<kb::script::ScriptModule>(std::move(scriptOptions));
    kb::script::ScriptModule* scriptModule = scriptModuleOwner.get();
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules;
    staticModules.push_back(std::move(scriptModuleOwner));

    kb::scene::Scene scene{ std::move(projectRuntime.descriptor), std::move(staticModules) };
    kb::world::WorldPartitionRuntime{scene}.SetMemoryProbe(kb::world::WorldMemoryKind::Gpu,
        [&renderer] { return kb::render::ProcessGpuMemoryBytes(renderer); });
    const bool scriptActive = scene.IsModuleActive("Script");
    if (scriptActive && (!scriptModule->Succeeded() || scriptModule->Host() == nullptr)) {
        std::cerr << "kb_game: script module initialization failed\n";
        for (const std::string& diagnostic : scriptModule->Diagnostics()) {
            std::cerr << "kb_game: script module diagnostic: " << diagnostic << '\n';
        }
        return EXIT_FAILURE;
    }

    if (scriptActive) {
        kb::world::WorldPartitionRuntime{scene}.SetMemoryProbe(kb::world::WorldMemoryKind::Scripts,
            [scriptModule]() -> std::optional<std::uint64_t> {
                return static_cast<std::uint64_t>(scriptModule->Host()->LuaRuntime().LuaMemoryUsedBytes());
            });
    }
    std::filesystem::path scenePath;
    std::size_t discoveredAssets = 0U;
    if (scriptActive && projectRuntime.IsPackaged()) {
        scriptModule->Host()->AssetPreparer().SetNativeSettings({
            .buildPlugins = false,
            .runtimeModuleRoot = projectRuntime.projectRoot,
        });
    }
    if (!LoadGameProjectScene(projectRuntime, scene, scenePath, discoveredAssets, std::cerr)) {
        return EXIT_FAILURE;
    }
    std::unique_ptr<kb::net::SceneClient> network;
    if (networkOptions) {
        network = std::make_unique<kb::net::SceneClient>(scene, std::move(*networkOptions),
            [](std::string& error) { return kb::net::CreateUdpTransport(kb::net::Endpoint::Make(0,0,0,0,0), error); });
    }
    if (!options.luaDebugDirectory.empty()) {
        std::string error;
        if (!scriptActive || !scriptModule->Host()->StartLuaDebugger(options.luaDebugDirectory, error)) {
            std::cerr << "kb_game: cannot start Lua debugger: " << error << '\n';
            return EXIT_FAILURE;
        }
    }
    renderer.SetRuntimeAssetDiscoveryEnabled(false);
    std::cout << "kb_game: project=" << kb::game::NarrowForDiagnostics(projectRuntime.projectRoot)
              << " scene=" << kb::game::NarrowForDiagnostics(scenePath)
              << " entities=" << scene.Entities().Count()
              << " assets=" << discoveredAssets
              << " modules=" << scene.ActiveModuleCount()
              << " backend=" << renderer.CapabilityReport().selectedBackendName
              << " resolution=" << window.Width() << 'x' << window.Height()
              << " sync=" << (options.uncapped || options.particleDiagnostics ? "uncapped" : "vsync")
              << " headless=" << options.headless
              << " gpu_vendor=" << renderer.CapabilityReport().vendorId
              << " gpu_device=" << renderer.CapabilityReport().deviceId << '\n';
    std::cout.flush();

    // A packaged game that sends crash reports asks the player first, on the first launch, and keeps the
    // answer with the player's settings; reports from earlier runs leave only after a yes. A development
    // run of a loose project keeps whatever consent the report folder already holds.
    std::optional<kb::platform::UserStorage> consentStorage;
    std::unique_ptr<kb::scene::CrashReportConsentFlow> crashReportConsent;
    if (projectRuntime.IsPackaged() && !userStorageRoot.empty()) {
        consentStorage.emplace(userStorageRoot, kb::script::kDefaultScriptUserStorageQuotaBytes);
        crashReportConsent = std::make_unique<kb::scene::CrashReportConsentFlow>(*consentStorage, kb::scene::CrashReportConsentHooks{
            .applyConsent = [](bool granted) {
                const std::filesystem::path directory = kb::platform::CrashReporter::ReportDirectory();
                return !directory.empty() && kb::platform::SetCrashUploadConsent(directory, granted);
            },
            .startUpload = [started = false]() mutable {
                if (!started) started = kb::platform::CrashReporter::StartPendingUpload();
            },
        });
        static_cast<void>(crashReportConsent->Begin(scene, !kb::platform::ConfiguredCrashUploadEndpoint().empty()));
    } else {
        static_cast<void>(kb::platform::CrashReporter::StartPendingUpload());
    }

    std::ofstream profileOutput;
    std::ofstream profileTraceOutput;
    std::vector<GameFrameProfile> profileRows;
    if (!options.profilePath.empty()) {
        profileOutput.open(options.profilePath, std::ios::trunc);
        if (!profileOutput) {
            std::cerr << "kb_game: profile file could not be opened: "
                      << kb::game::NarrowForDiagnostics(options.profilePath) << '\n';
            return EXIT_FAILURE;
        }
        profileOutput.imbue(std::locale::classic());
        auto tracePath = options.profilePath;
        tracePath += ".trace.jsonl";
        profileTraceOutput.open(tracePath, std::ios::trunc);
        if (!profileTraceOutput) {
            std::cerr << "kb_game: frame trace file could not be opened\n";
            return EXIT_FAILURE;
        }
        profileOutput << "frame,processing_wall_ms,simulation_ms,render_ms,begin_submit_ms,end_frame_ms,gpu_delayed_ms,bgfx_wait_render_ms,bgfx_wait_submit_ms,draws,shadow_casters,submitted_meshes,dropped,missing_resources,fixed_steps,exposure_readback_submitted,exposure_sample_available,width,height,wall_frame_ms,visible_meshes,culled_instances,scene_lights,submitted_lights,skipped_lights,light_capacity,lighting_path,shadow_draws,instance_upload_bytes,transform_ms,scene_sync_ms,transform_inspected,transform_updated,mesh_command_reuse,scene_entities,streaming_operations,streaming_ms,render_camera_x,render_camera_y,render_camera_z,render_camera_valid,world_budget_bytes,world_cell_estimate_bytes,world_resident_units,world_measured_bytes,world_estimated_bytes,world_error_ratio,world_cpu_source,world_cpu_bytes,world_cpu_baseline_bytes,world_instances_source,world_instances_bytes,world_instances_baseline_bytes,world_scripts_source,world_scripts_bytes,world_scripts_baseline_bytes,world_physics_source,world_physics_bytes,world_physics_baseline_bytes,world_gpu_source,world_gpu_bytes,world_gpu_baseline_bytes,world_known_payload_bytes,world_unmeasured_payloads,world_baseline_unmeasured_payloads,scene_update_ms,fixed_simulation_ms,ecs_systems_ms\n";
        auto metadataPath = options.profilePath; metadataPath += ".meta.json";
        std::ofstream metadataOutput(metadataPath, std::ios::trunc);
        auto metadata = kb::core::JsonValue::MakeObject();
        const auto& capabilities = renderer.CapabilityReport();
        metadata.Set("schema", kb::core::JsonValue::MakeString("21kb.scene-profile/v1"));
        metadata.Set("backend", kb::core::JsonValue::MakeString(capabilities.selectedBackendName));
        metadata.Set("vendor_id", kb::core::JsonValue::MakeNumber(capabilities.vendorId));
        metadata.Set("device_id", kb::core::JsonValue::MakeNumber(capabilities.deviceId));
        metadata.Set("requested_backend", kb::core::JsonValue::MakeNumber(options.rendererType));
        metadata.Set("width", kb::core::JsonValue::MakeNumber(window.Width()));
        metadata.Set("height", kb::core::JsonValue::MakeNumber(window.Height()));
        metadata.Set("frame_limit", kb::core::JsonValue::MakeNumber(options.frameLimit));
        metadata.Set("fixed_step", kb::core::JsonValue::MakeBool(options.profileFixedStep));
        metadata.Set("headless", kb::core::JsonValue::MakeBool(options.headless));
        metadata.Set("uncapped", kb::core::JsonValue::MakeBool(options.uncapped || options.particleDiagnostics));
        metadata.Set("overdraw_enabled", kb::core::JsonValue::MakeBool(options.profileSceneOverdraw));
        metadata.Set("driver_version", kb::core::JsonValue::MakeNull());
        metadata.Set("driver_version_basis", kb::core::JsonValue::MakeString("bgfx does not expose a portable driver version; the profiling harness records the OS adapter driver"));
        metadataOutput << metadata.Dump() << '\n';
        metadataOutput.flush();
        if (!metadataOutput) { std::cerr << "kb_game: profile metadata could not be written\n"; return EXIT_FAILURE; }
        profileRows.reserve(options.frameLimit);
        scene.Runtime().SetEcsProfilerEnabled(true);
        renderer.SetMeshCoverageEnabled(options.profileSceneOverdraw);
    }
    kb::input::Win32XInputHapticsBackend hapticsBackend;
    kb::input::InputHaptics::RegisterBackend(scene, hapticsBackend);

    std::uint32_t renderedFrames = 0U;
    std::uint32_t submittedFrames = 0U;
    std::uint32_t observedParticleCompute = 0U;
    std::uint32_t observedParticleDraws = 0U;
    bool runtimeClean = true;
    std::uint32_t reportedShadowDrops = 0U, reportedGiBoxes = 0U;
    // A shipped game outlives faults in its content. Each distinct script,
    // scene-system or render error is logged once with the entity and asset it
    // concerns; the script host disables the failing behaviour, the renderer
    // draws with its error material or skips a draw it has no mesh for, and the
    // loop carries on. Only losing the graphics device ends the run early. The
    // exit code still reports that the run was not clean.
    std::unordered_set<std::string> reportedErrors;
    std::size_t degradedErrors = 0U;
    const auto reportError = [&](std::string message) {
        runtimeClean = false;
        if (reportedErrors.insert(message).second) {
            ++degradedErrors;
            std::cerr << "kb_game: " << message << '\n';
        }
    };
    kb::game::RuntimeSceneFrameSync renderSceneSync;
    renderer.PrepareParticleScene(scene);
    auto previousTick = std::chrono::steady_clock::now();
    const auto particleDeadline = previousTick + std::chrono::minutes(2);
    while (window.PumpMessages() && !scene.Runtime().ShouldQuit()) {
        if (options.particleDiagnostics && std::chrono::steady_clock::now() > particleDeadline) {
            reportError("particle diagnostics exceeded two minutes");
            break;
        }
        if (window.Width() == 0U || window.Height() == 0U) {
            if (!options.profilePath.empty() || options.particleDiagnostics) {
                std::cerr << "kb_game: benchmark interrupted by minimization\n";
                runtimeClean = false;
                break;
            }
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
        const auto profileBegin = !options.profilePath.empty()
            ? now : std::chrono::steady_clock::time_point{};
        const double wallFrameMilliseconds = std::chrono::duration<double, std::milli>(now - previousTick).count();
        const float deltaSeconds = !inputReplay.empty() ? inputReplay[renderedFrames].deltaSeconds :
            (options.profileFixedStep || options.particleDiagnostics
                ? 1.0F / 60.0F : kb::game::RuntimeDeltaSeconds(previousTick, now));
        previousTick = now;

        if (!inputReplay.empty()) kb::input::ApplyInputFrame(scene.Input().MutableDeviceState(), inputReplay[renderedFrames]);
        else if (!options.headless) inputCollector.Collect(scene, window.Handle());
        static_cast<void>(scene.UI().SetViewport(
            static_cast<float>(window.Width()),
            static_cast<float>(window.Height())));
        renderSceneSync.BeforeUpdate(scene);
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
        if (network && !network->Update(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()), std::cerr)) {
            reportError("network session update failed");
            break;
        }
        if (!options.headless) inputCollector.SynchronizeTextInput(scene, window.Handle());
        for (std::string& error : kb::game::DrainGameRuntimeErrors(
                 scene, scriptActive ? scriptModule : nullptr)) {
            reportError(std::move(error));
        }
        const auto profileSimulationEnd = !options.profilePath.empty()
            ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        renderer.SetFrameDeltaSeconds(deltaSeconds);
        if (!renderer.BeginFrame()) {
            std::cerr << "kb_game: renderer could not begin a frame\n";
            runtimeClean = false;
            break;
        }
        const bool submitted = renderSceneSync.Submit(scene, renderer);
        const auto& renderStats = renderer.LastSceneSubmitStats();
        const bool renderErrors = renderer.LastSceneDiagnostics().HasErrors() ||
            renderStats.HasMissingResources() || renderStats.droppedInstanceCount != 0U;
        const auto profileAfterSubmit = !options.profilePath.empty()
            ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (submitted && !renderErrors && renderedFrames + 1U == options.screenshotFrame) {
            // Capture the final backbuffer, including display output and screen UI.
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, options.screenshotPath.string().c_str());
        }
        renderer.EndFrame();
        if (options.particleDiagnostics) {
            const auto* stats = bgfx::getStats();
            if (stats) observedParticleCompute = std::max(observedParticleCompute, stats->numCompute);
            observedParticleDraws = std::max(observedParticleDraws, renderStats.submittedParticleDrawCallCount);
        }
        if (!options.profilePath.empty()) {
            const auto renderCamera = kb::scene::SceneRenderFeedback::ScreenPointToRay(scene,
                static_cast<float>(window.Width()) * 0.5F, static_cast<float>(window.Height()) * 0.5F);
            const auto profileEnd = std::chrono::steady_clock::now();
            const bgfx::Stats* gpu = bgfx::getStats();
            bool readbackSubmitted = false;
            bool exposureSampleAvailable = false;
            for (const auto& exposure : renderer.LastSceneExposureStats()) {
                readbackSubmitted |= exposure.gpuReadbackSubmitted;
                exposureSampleAvailable |= exposure.gpuReadbackSampleAvailable;
            }
            const double gpuMilliseconds = gpu != nullptr && gpu->gpuTimerFreq > 0 &&
                gpu->gpuTimeEnd > gpu->gpuTimeBegin
                ? static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) * 1000.0 /
                    static_cast<double>(gpu->gpuTimerFreq)
                : -1.0;
            const double cpuTickMilliseconds = gpu != nullptr && gpu->cpuTimerFreq > 0
                ? 1000.0 / static_cast<double>(gpu->cpuTimerFreq) : 0.0;
            const auto hotPath = scene.Runtime().HotPathReport();
            using kb::core::JsonValue;
            auto frameTrace = JsonValue::MakeObject();
            frameTrace.Set("schema", JsonValue::MakeString("21kb.frame-trace/v1"));
            frameTrace.Set("frame", JsonValue::MakeNumber(renderedFrames));
            frameTrace.Set("basis", JsonValue::MakeString("elapsed intervals on a shared steady clock; ECS system times are relative to ecs_scheduler_origin_ns"));
            auto rendering = JsonValue::MakeObject();
            rendering.Set("scene_sync_ms", JsonValue::MakeNumber(renderer.LastSceneSynchronizationMilliseconds()));
            rendering.Set("draw_count", JsonValue::MakeNumber(renderStats.submittedDrawCallCount));
            rendering.Set("gi_triangle_meshes", JsonValue::MakeNumber(renderStats.giGeometryMeshCount));
            rendering.Set("gi_box_meshes", JsonValue::MakeNumber(renderStats.giBoundingBoxMeshCount));
            rendering.Set("shadow_lights_selected", JsonValue::MakeNumber(renderStats.selectedShadowLightCount));
            rendering.Set("shadow_lights_dropped", JsonValue::MakeNumber(renderStats.droppedShadowLightCount));
            const auto resourceMemory = renderer.RuntimeResourceStats().memory;
            auto memory = JsonValue::MakeObject();
            const auto estimate = [&](const char* name, std::uint64_t bytes) { memory.Set(name, JsonValue::MakeString(std::to_string(bytes))); };
            estimate("mesh_payload_bytes", resourceMemory.meshBytes);
            estimate("texture_payload_bytes", resourceMemory.textureBytes);
            estimate("pending_mesh_payload_bytes", resourceMemory.pendingMeshBytes);
            estimate("pending_texture_payload_bytes", resourceMemory.pendingTextureBytes);
            estimate("retained_payload_bytes", resourceMemory.RetainedBytes());
            estimate("peak_retained_payload_bytes", resourceMemory.peakRetainedBytes);
            estimate("allocated_payload_bytes", resourceMemory.allocatedBytes);
            estimate("released_payload_bytes", resourceMemory.releasedBytes);
            memory.Set("registry_basis", JsonValue::MakeString("logical mesh/texture payload estimates including deferred destruction; excludes staging, driver padding and resources owned outside the registry; not measured residency"));
            const auto bgfxBytes = [&](const char* name, std::int64_t bytes) { memory.Set(name,
                bytes >= 0 ? JsonValue::MakeString(std::to_string(bytes)) : JsonValue::MakeNull()); };
            bgfxBytes("bgfx_texture_estimate_bytes", gpu ? gpu->textureMemoryUsed : -1);
            bgfxBytes("bgfx_render_target_estimate_bytes", gpu ? gpu->rtMemoryUsed : -1);
            bgfxBytes("driver_gpu_used_bytes", gpu ? gpu->gpuMemoryUsed : -1);
            bgfxBytes("driver_gpu_max_bytes", gpu ? gpu->gpuMemoryMax : -1);
            memory.Set("driver_basis", JsonValue::MakeString("backend-reported GPU memory; null when unavailable; asynchronous, backend-specific scope, including the entire adapter on D3D11/NVAPI; not scene or application residency"));
            rendering.Set("memory", std::move(memory));
            rendering.Set("gpu_ms", gpuMilliseconds >= 0.0 ? JsonValue::MakeNumber(gpuMilliseconds) : JsonValue::MakeNull());
            rendering.Set("bgfx_frame", JsonValue::MakeNumber(renderer.LastCompletedFrame()));
            rendering.Set("gpu_frame", gpu != nullptr ? JsonValue::MakeNumber(gpu->gpuFrameNum) : JsonValue::MakeNull());
            rendering.Set("gpu_time_basis", JsonValue::MakeString(options.profileSceneOverdraw
                ? "delayed bgfx GPU frame timing including diagnostic mesh replays; unavailable when the backend does not expose it"
                : "delayed bgfx GPU frame timing; unavailable when the backend does not expose it"));
            const auto coverage = renderer.LastMeshCoverage();
            const double pixelCount = static_cast<double>(coverage.width) * coverage.height;
            rendering.Set("overdraw", coverage.available && pixelCount > 0.0
                ? JsonValue::MakeNumber(static_cast<double>(coverage.passingSamples + coverage.particleSamples) / pixelCount)
                : JsonValue::MakeNull());
            rendering.Set("overdraw_basis", JsonValue::MakeString("delayed depth-independent GPU replay of 3D mesh and particle samples per target pixel; excludes UI, sky, shadow and post-processing and adds GPU cost"));
            auto meshCoverage = JsonValue::MakeObject();
            meshCoverage.Set("enabled", JsonValue::MakeBool(options.profileSceneOverdraw));
            meshCoverage.Set("available", JsonValue::MakeBool(coverage.available));
            meshCoverage.Set("samples_per_pixel", coverage.available && coverage.width && coverage.height
                ? JsonValue::MakeNumber(static_cast<double>(coverage.passingSamples) /
                    (static_cast<double>(coverage.width) * coverage.height)) : JsonValue::MakeNull());
            meshCoverage.Set("passing_samples", coverage.available ? JsonValue::MakeNumber(static_cast<double>(coverage.passingSamples)) : JsonValue::MakeNull());
            meshCoverage.Set("frame", coverage.available ? JsonValue::MakeNumber(static_cast<double>(coverage.frame)) : JsonValue::MakeNull());
            meshCoverage.Set("draws", JsonValue::MakeNumber(coverage.measuredDraws));
            meshCoverage.Set("unavailable_reason", coverage.available ? JsonValue::MakeNull() : JsonValue::MakeString(
                options.profileSceneOverdraw ? "no complete GPU query result is available" : "measurement was not enabled"));
            meshCoverage.Set("basis", JsonValue::MakeString("delayed GPU samples from depth-independent mesh replay; excludes particles, UI, sky, shadows and post-processing; adds draw and shader cost"));
            rendering.Set("mesh_overdraw", std::move(meshCoverage));
            auto particleCoverage = JsonValue::MakeObject();
            particleCoverage.Set("passing_samples", coverage.available ? JsonValue::MakeNumber(static_cast<double>(coverage.particleSamples)) : JsonValue::MakeNull());
            particleCoverage.Set("draws", JsonValue::MakeNumber(coverage.particleDraws));
            rendering.Set("particle_overdraw", std::move(particleCoverage));
            frameTrace.Set("rendering", std::move(rendering));
            const auto nsFromFrame = [profileBegin](auto time) {
                return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(time - profileBegin).count());
            };
            const auto frameOrigin = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(profileBegin.time_since_epoch()).count());
            const double sceneOffset = static_cast<double>(hotPath.runtimeUpdateStartSteadyNanoseconds - frameOrigin);
            auto stages = JsonValue::MakeArray();
            for (const auto& stage : hotPath.runtimeStages) {
                auto entry = JsonValue::MakeObject();
                entry.Set("name", JsonValue::MakeString(stage.name));
                entry.Set("start_ns", JsonValue::MakeNumber(sceneOffset + static_cast<double>(stage.startNanoseconds)));
                entry.Set("end_ns", JsonValue::MakeNumber(sceneOffset + static_cast<double>(stage.endNanoseconds)));
                stages.Append(std::move(entry));
            }
            auto renderStage = JsonValue::MakeObject();
            renderStage.Set("name", JsonValue::MakeString("render.submit_and_frame"));
            renderStage.Set("start_ns", JsonValue::MakeNumber(nsFromFrame(profileSimulationEnd)));
            renderStage.Set("end_ns", JsonValue::MakeNumber(nsFromFrame(profileEnd)));
            stages.Append(std::move(renderStage));
            frameTrace.Set("stages", std::move(stages));
            auto sceneSystems = JsonValue::MakeArray();
            for (const auto& timing : hotPath.sceneSystems) {
                auto entry = JsonValue::MakeObject();
                entry.Set("type", JsonValue::MakeString(timing.systemType));
                entry.Set("phase", JsonValue::MakeString(timing.phase));
                entry.Set("start_ns", JsonValue::MakeNumber(sceneOffset + static_cast<double>(timing.startNanoseconds)));
                entry.Set("end_ns", JsonValue::MakeNumber(sceneOffset + static_cast<double>(timing.endNanoseconds)));
                entry.Set("worker", timing.ownerThread ? JsonValue::MakeNull() : JsonValue::MakeNumber(static_cast<double>(timing.workerIndex)));
                entry.Set("dispatch_group", JsonValue::MakeNumber(static_cast<double>(timing.dispatchGroup)));
                entry.Set("queue_wait_ns", JsonValue::MakeNumber(static_cast<double>(timing.queueWaitNanoseconds)));
                sceneSystems.Append(std::move(entry));
            }
            frameTrace.Set("scene_systems", std::move(sceneSystems));
            auto systems = JsonValue::MakeArray();
            const auto& ecsTrace = scene.Runtime().LastEcsProfilerTrace();
            frameTrace.Set("ecs_scheduler_origin_ns", ecsTrace.frameStartSteadyNanoseconds >= frameOrigin
                ? JsonValue::MakeNumber(static_cast<double>(ecsTrace.frameStartSteadyNanoseconds - frameOrigin)) : JsonValue::MakeNull());
            for (const auto& event : ecsTrace.events) {
                auto entry = JsonValue::MakeObject();
                entry.Set("name", JsonValue::MakeString(event.systemName));
                entry.Set("worker", JsonValue::MakeNumber(static_cast<double>(event.workerIndex)));
                entry.Set("start_ns", JsonValue::MakeNumber(static_cast<double>(event.startTimeNanoseconds)));
                entry.Set("end_ns", JsonValue::MakeNumber(static_cast<double>(event.endTimeNanoseconds)));
                entry.Set("wait_ns", JsonValue::MakeNumber(static_cast<double>(event.waitTimeNanoseconds)));
                auto dependencies = JsonValue::MakeArray();
                for (const auto& dependency : event.blockedDependencies) dependencies.Append(JsonValue::MakeString(dependency));
                entry.Set("dependencies", std::move(dependencies));
                systems.Append(std::move(entry));
            }
            frameTrace.Set("ecs_systems_relative_to_scheduler", std::move(systems));
            profileTraceOutput << frameTrace.Dump() << '\n';
            double sceneUpdateMilliseconds = 0.0, fixedSimulationMilliseconds = 0.0, ecsSystemsMilliseconds = 0.0;
            for (const auto& stage : hotPath.runtimeStages) {
                const double elapsed = static_cast<double>(stage.endNanoseconds - stage.startNanoseconds) / 1e6;
                if (stage.name == "scene.update.pre_fixed" || stage.name == "scene.update.post_fixed") sceneUpdateMilliseconds += elapsed;
                else if (stage.name == "scene.fixed.simulation") fixedSimulationMilliseconds += elapsed;
                else if (stage.name == "ecs.systems") ecsSystemsMilliseconds += elapsed;
            }
            profileRows.push_back(GameFrameProfile{
                .wallFrameMilliseconds = wallFrameMilliseconds,
                .cpuMilliseconds = std::chrono::duration<double, std::milli>(profileEnd - profileBegin).count(),
                .simulationMilliseconds = std::chrono::duration<double, std::milli>(
                    profileSimulationEnd - profileBegin).count(),
                .renderMilliseconds = std::chrono::duration<double, std::milli>(
                    profileEnd - profileSimulationEnd).count(),
                .beginSubmitMilliseconds = std::chrono::duration<double, std::milli>(
                    profileAfterSubmit - profileSimulationEnd).count(),
                .endFrameMilliseconds = std::chrono::duration<double, std::milli>(
                    profileEnd - profileAfterSubmit).count(),
                .gpuDelayedMilliseconds = gpuMilliseconds,
                .bgfxWaitRenderMilliseconds = cpuTickMilliseconds > 0.0
                    ? static_cast<double>(gpu->waitRender) * cpuTickMilliseconds : -1.0,
                .bgfxWaitSubmitMilliseconds = cpuTickMilliseconds > 0.0
                    ? static_cast<double>(gpu->waitSubmit) * cpuTickMilliseconds : -1.0,
                .draws = renderStats.submittedDrawCallCount,
                .shadowCasters = renderStats.shadowCasterCount,
                .submittedMeshes = renderStats.submittedMeshCount,
                .droppedInstances = renderStats.droppedInstanceCount,
                .missingResources = renderStats.HasMissingResources() ? 1U : 0U,
                .fixedSteps = static_cast<std::uint32_t>(scene.Runtime().LastFixedStepCount()),
                .exposureReadbackSubmitted = readbackSubmitted ? 1U : 0U,
                .exposureSampleAvailable = exposureSampleAvailable ? 1U : 0U,
                .width = window.Width(),
                .height = window.Height(),
                .visibleMeshes = renderStats.visibleMeshCount,
                .culledInstances = renderStats.culledInstanceCount,
                .sceneLights = renderStats.sceneLightCount,
                .submittedLights = renderStats.submittedForwardLightCount,
                .skippedLights = renderStats.skippedForwardLightCount,
                .lightCapacity = renderStats.forwardLightCapacity,
                .lightingPath = renderStats.lightingPath,
                .shadowDraws = renderStats.submittedShadowDrawCallCount,
                .instanceUploadBytes = renderStats.instanceUploadBytes,
                .transformMilliseconds = static_cast<double>(hotPath.runtimeTransformSyncNanoseconds) / 1e6,
                .sceneSyncMilliseconds = renderer.LastSceneSynchronizationMilliseconds(),
                .transformInspected = static_cast<std::uint32_t>(hotPath.transformHierarchyInspectedCount),
                .transformUpdated = static_cast<std::uint32_t>(hotPath.transformHierarchyUpdatedCount),
                .meshCommandReuseCount = renderStats.meshCommandReuseCount,
                .sceneEntities = static_cast<std::uint32_t>(scene.Entities().Count()),
                .streamingOperations = static_cast<std::uint32_t>(scene.LoadedContent().StreamingStats().operations),
                .streamingMilliseconds = scene.LoadedContent().StreamingStats().milliseconds,
                .renderCameraX = renderCamera.ray.origin.x,
                .renderCameraY = renderCamera.ray.origin.y,
                .renderCameraZ = renderCamera.ray.origin.z,
                .renderCameraValid = renderCamera.valid,
                .worldMemory = kb::world::WorldPartitionRuntime{scene}.MemoryReport(),
                .sceneUpdateMilliseconds = sceneUpdateMilliseconds,
                .fixedSimulationMilliseconds = fixedSimulationMilliseconds,
                .ecsSystemsMilliseconds = ecsSystemsMilliseconds,
            });
        }
        if (!submitted) {
            reportError("renderer did not accept the scene; the frame was not drawn");
        }
        if (renderStats.droppedShadowLightCount != 0U && renderStats.droppedShadowLightCount != reportedShadowDrops) {
            std::cerr << "kb_game: renderer warning: " << renderStats.droppedShadowLightCount
                      << " local lights exceed the shadow atlas limit; those lights render without shadows\n";
        }
        if (renderStats.giBoundingBoxMeshCount != 0U && renderStats.giBoundingBoxMeshCount != reportedGiBoxes) {
            std::cerr << "kb_game: renderer warning: GI uses solid bounding boxes for " << renderStats.giBoundingBoxMeshCount
                      << " meshes without CPU triangles; publish a current GI proxy through SetMeshProxyGeometry\n";
        }
        reportedShadowDrops = renderStats.droppedShadowLightCount;
        reportedGiBoxes = renderStats.giBoundingBoxMeshCount;
        if (renderStats.droppedInstanceCount != 0U) {
            reportError("renderer dropped instances from the frame");
        }
        if (renderStats.HasMissingResources()) {
            reportError("renderer has missing resources: mesh bindings=" + std::to_string(renderStats.missingMeshBindingCount) +
                " meshes=" + std::to_string(renderStats.missingMeshResourceCount) +
                " vertex formats=" + std::to_string(renderStats.unsupportedMeshVertexFormatCount) +
                " material bindings=" + std::to_string(renderStats.missingMaterialBindingCount) +
                " materials=" + std::to_string(renderStats.missingMaterialResourceCount) +
                " texture bindings=" + std::to_string(renderStats.missingTextureBindingCount) +
                " textures=" + std::to_string(renderStats.missingTextureResourceCount) +
                " texture dimensions=" + std::to_string(renderStats.textureDimensionMismatchCount));
        }
        if (renderErrors) {
            for (const auto& event : renderer.LastSceneDiagnostics().events) {
                if (event.severity != kb::render::SceneRenderDiagnosticSeverity::Info) {
                    reportError(std::string{ event.severity == kb::render::SceneRenderDiagnosticSeverity::Error ? "render error" : "render warning" }
                        + " kind=" + std::to_string(static_cast<unsigned>(event.kind))
                        + " entity=" + std::to_string(event.entityId) + " mesh=" + std::to_string(event.meshAssetId)
                        + " material=" + std::to_string(event.materialAssetId) + " texture=" + std::to_string(event.textureAssetId)
                        + " profile=" + std::to_string(event.postProcessProfileAssetId)
                        + " particle_instance=" + std::to_string(event.particleInstanceId)
                        + " particle_emitter=" + std::to_string(event.particleEmitterId)
                        + " particle_update=" + std::string{kb::render::ParticleGpuUpdateFailureName(event.particleGpuUpdateFailure)}
                        + (event.kind == kb::render::SceneRenderDiagnosticKind::ParticleGpuUpdateRejected
                            ? "; retained the last complete GPU emitter state" : "; drawn with the fallback material or skipped"));
                }
            }
        }
        if (submitted) {
            ++submittedFrames;
        }
        ++renderedFrames;
        if (options.frameLimit != 0U && renderedFrames >= options.frameLimit) {
            break;
        }
    }

    if (options.particleDiagnostics) {
        auto capture = kb::game::CaptureParticleRuntimeDiagnostics(scene, renderer, options.particleSamples,
            projectRuntime.IsPackaged(), submittedFrames, observedParticleCompute, observedParticleDraws);
        const bool succeeded = capture.succeeded && runtimeClean && renderedFrames == options.frameLimit &&
            scene.Runtime().FixedStepIndex() == options.frameLimit &&
            (!options.particleRequireGpu || capture.gpuExecutionObserved);
        capture.report.Set("succeeded", kb::core::JsonValue::MakeBool(succeeded));
        std::cout << "kb_game: particle_diagnostics=" << capture.report.Dump() << '\n';
        if (!succeeded) reportError("particle diagnostics did not satisfy the execution/readback requirements");
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
    for (std::string& error : kb::game::DrainGameRuntimeErrors(
             scene, scriptActive ? scriptModule : nullptr)) {
        reportError(std::move(error));
    }
    hapticsBackend.StopAll();
    network.reset();
    kb::input::InputHaptics::UnregisterBackend(scene, hapticsBackend);
    renderer.ReleaseScene(scene);
    renderer.Shutdown();
    if (!options.screenshotPath.empty()) {
        std::error_code error;
        const auto size = std::filesystem::file_size(options.screenshotPath, error);
        if (renderedFrames < options.screenshotFrame || error || size == 0U) {
            std::cerr << "kb_game: final screenshot was not written\n";
            runtimeClean = false;
        }
    }
    if (!options.profilePath.empty()) {
        for (std::size_t index = 0U; index < profileRows.size(); ++index) {
            const GameFrameProfile& row = profileRows[index];
            profileOutput << index << ',' << row.cpuMilliseconds << ',' << row.simulationMilliseconds << ','
                          << row.renderMilliseconds << ',' << row.beginSubmitMilliseconds << ','
                          << row.endFrameMilliseconds << ',' << row.gpuDelayedMilliseconds << ','
                          << row.bgfxWaitRenderMilliseconds << ',' << row.bgfxWaitSubmitMilliseconds << ','
                          << row.draws << ',' << row.shadowCasters << ',' << row.submittedMeshes << ','
                          << row.droppedInstances << ',' << row.missingResources << ',' << row.fixedSteps << ','
                          << row.exposureReadbackSubmitted << ',' << row.exposureSampleAvailable << ','
                          << row.width << ',' << row.height << ',' << row.wallFrameMilliseconds << ','
                          << row.visibleMeshes << ',' << row.culledInstances << ',' << row.sceneLights << ','
                          << row.submittedLights << ',' << row.skippedLights << ',' << row.lightCapacity << ','
                          << row.lightingPath << ',' << row.shadowDraws << ',' << row.instanceUploadBytes << ','
                          << row.transformMilliseconds << ',' << row.sceneSyncMilliseconds << ','
                          << row.transformInspected << ',' << row.transformUpdated << ',' << row.meshCommandReuseCount << ','
                          << row.sceneEntities << ',' << row.streamingOperations << ',' << row.streamingMilliseconds << ','
                          << row.renderCameraX << ',' << row.renderCameraY << ',' << row.renderCameraZ << ','
                          << row.renderCameraValid;
            WriteWorldMemoryProfile(profileOutput, row.worldMemory) << ',' << row.sceneUpdateMilliseconds << ','
                          << row.fixedSimulationMilliseconds << ',' << row.ecsSystemsMilliseconds << '\n';
        }
        profileOutput.flush();
        profileTraceOutput.flush();
        if (!profileOutput || !profileTraceOutput) {
            std::cerr << "kb_game: profile file could not be written\n";
            runtimeClean = false;
        }
    }

    // frames counts loop iterations; rendered counts the ones the renderer
    // actually accepted, ticks the ones the scene runtime actually stepped and
    // simulated the time it was stepped by. Reporting only the first would let a
    // loop that draws nothing and simulates nothing look identical to one that
    // does both.
    std::cout << "kb_game: frames=" << renderedFrames
              << " shutdown=" << (shutdownClean ? "clean" : "incomplete")
              << " rendered=" << submittedFrames
              << " ticks=" << scene.Runtime().FrameIndex()
              << " simulated=" << scene.Runtime().ElapsedSeconds()
              << " errors=" << degradedErrors << '\n';
    std::cout.flush();
    return shutdownClean && runtimeClean ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // First, so any crash after this point leaves a minidump in the player's
    // crash report folder instead of ending the game without a trace. Reports
    // earlier runs left wait until RunGame knows the player's answer.
    kb::platform::CrashReporterOptions crashReporterOptions;
    crashReporterOptions.uploadPendingReports = false;
    static_cast<void>(kb::platform::CrashReporter::Install(crashReporterOptions));
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
        const bool shipped = kb::game::IsShippedGamePlayer();
        const std::wstring_view developmentSwitch =
            shipped ? FirstDevelopmentSwitch(argc, argv) : std::wstring_view{};
        if (!developmentSwitch.empty()) {
            std::cerr << "kb_game: " << kb::game::NarrowForDiagnostics(developmentSwitch)
                      << " is a development option; a packaged game runs only its own "
                      << kb::game::kPackagedGameFileName << '\n';
            LocalFree(argv);
            return EXIT_FAILURE;
        }
        const bool parsed = ParseArguments(argc, argv, options);
        LocalFree(argv);
        if (!parsed) {
            return EXIT_FAILURE;
        }
        if (options.help) {
            return EXIT_SUCCESS;
        }
        // A packaged game keeps its project beside the executable, so an
        // argument-less launch starts the project's own ProjectSettings::defaultMap.
        // A shipped game never names any other project.
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
