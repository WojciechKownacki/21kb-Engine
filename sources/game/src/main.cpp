#include "GameProjectRuntime.hpp"
#include "GameWindow.hpp"
#include "RuntimeSceneFrameSync.hpp"

#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"

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
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultWindowWidth = 1280U;
constexpr std::uint32_t kDefaultWindowHeight = 720U;

struct GameOptions {
    std::filesystem::path projectPath;
    std::string sceneOverride;
    std::uint32_t width = kDefaultWindowWidth;
    std::uint32_t height = kDefaultWindowHeight;
    bool fullscreen = false;
    bool headless = false;
    bool uncapped = false;
    bool profileFixedStep = false;
    std::filesystem::path profilePath;
    std::optional<kb::render::SceneRenderLightingPath> profileLighting;
    std::filesystem::path screenshotPath;
    std::uint32_t screenshotFrame = 0U;
    // 0 runs until the player closes the window; a positive value bounds the
    // run so an automated check can drive the real executable to completion.
    std::uint32_t frameLimit = 0U;
};

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

[[nodiscard]] bool ParseWindowExtent(std::wstring_view text, std::uint32_t& extent) noexcept {
    if (!ParseFrameLimit(text, extent) || extent < 64U || extent > 8192U) {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseArguments(int argc, wchar_t** argv, GameOptions& options) {
    bool explicitWidth = false;
    bool explicitHeight = false;
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
        } else if (argument == L"--headless") {
            options.headless = true;
        } else if (argument == L"--profile-fixed-step") {
            options.profileFixedStep = true;
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
        } else if (HasPrefix(argument, L"--profile-lighting=")) {
            const auto value = argument.substr(19U);
            if (value == L"Forward") options.profileLighting = kb::render::SceneRenderLightingPath::Forward;
            else if (value == L"ForwardPlus") options.profileLighting = kb::render::SceneRenderLightingPath::ClusteredForwardPlus;
            else if (value == L"Deferred") options.profileLighting = kb::render::SceneRenderLightingPath::Deferred;
            else {
                std::cerr << "kb_game: --profile-lighting expects Forward, ForwardPlus or Deferred\n";
                return false;
            }
        } else if (HasPrefix(argument, L"--profile-file=")) {
            if (argument.size() == 15U) {
                std::cerr << "kb_game: --profile-file requires a path\n";
                return false;
            }
            options.profilePath = std::filesystem::path{std::wstring{argument.substr(15U)}};
        } else {
            std::cerr << "kb_game: unknown option '"
                      << kb::game::NarrowForDiagnostics(argument) << "'\n";
            return false;
        }
    }
    if (options.headless && options.fullscreen) {
        std::cerr << "kb_game: --headless cannot use --fullscreen\n";
        return false;
    }
    if (options.profileFixedStep && (!options.headless || options.profilePath.empty())) {
        std::cerr << "kb_game: --profile-fixed-step requires --headless and --profile-file\n";
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
    if (options.fullscreen &&
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == 0 &&
        AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == 0) {
        std::cerr << "kb_game: fullscreen requires per-monitor DPI awareness\n";
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
    if (options.uncapped) {
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
        // A cache directory whose name cannot be spelled exactly in this
        // machine's code page is worse than no cache directory.
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
        lighting.maxForwardLights = lighting.lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus
            ? kb::render::kMaxSceneForwardPlusLights : kb::render::kMaxSceneForwardLights;
        renderer.SetDefaultSceneLightingConfig(lighting);
    }

    kb::script::ScriptModuleOptions scriptOptions;
    scriptOptions.runtimeOptions.userStorageRoot = GameUserStorageRoot(projectRuntime);
    auto scriptModuleOwner = std::make_unique<kb::script::ScriptModule>(std::move(scriptOptions));
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
    if (scriptActive && projectRuntime.IsPackaged()) {
        scriptModule->Host()->AssetPreparer().SetNativeSettings({
            .buildPlugins = false,
            .runtimeModuleRoot = projectRuntime.projectRoot,
        });
    }
    if (!LoadGameProjectScene(projectRuntime, scene, scenePath, discoveredAssets, std::cerr)) {
        return EXIT_FAILURE;
    }
    renderer.SetRuntimeAssetDiscoveryEnabled(false);
    std::cout << "kb_game: project=" << kb::game::NarrowForDiagnostics(projectRuntime.projectRoot)
              << " scene=" << kb::game::NarrowForDiagnostics(scenePath)
              << " entities=" << scene.Entities().Count()
              << " assets=" << discoveredAssets
              << " modules=" << scene.ActiveModuleCount()
              << " backend=" << renderer.CapabilityReport().selectedBackendName
              << " resolution=" << window.Width() << 'x' << window.Height()
              << " sync=" << (options.uncapped ? "uncapped" : "vsync")
              << " headless=" << options.headless
              << " gpu_vendor=" << renderer.CapabilityReport().vendorId
              << " gpu_device=" << renderer.CapabilityReport().deviceId << '\n';
    std::cout.flush();

    std::ofstream profileOutput;
    std::vector<GameFrameProfile> profileRows;
    if (!options.profilePath.empty()) {
        profileOutput.open(options.profilePath, std::ios::trunc);
        if (!profileOutput) {
            std::cerr << "kb_game: profile file could not be opened: "
                      << kb::game::NarrowForDiagnostics(options.profilePath) << '\n';
            return EXIT_FAILURE;
        }
        profileOutput.imbue(std::locale::classic());
        profileOutput << "frame,cpu_ms,simulation_ms,render_ms,begin_submit_ms,end_frame_ms,gpu_delayed_ms,bgfx_wait_render_ms,bgfx_wait_submit_ms,draws,shadow_casters,submitted_meshes,dropped,missing_resources,fixed_steps,exposure_readback_submitted,exposure_sample_available,width,height,wall_frame_ms,visible_meshes,culled_instances,scene_lights,submitted_lights,skipped_lights,light_capacity,lighting_path,shadow_draws,instance_upload_bytes,transform_ms,scene_sync_ms,transform_inspected,transform_updated,mesh_command_reuse,scene_entities,streaming_operations,streaming_ms,render_camera_x,render_camera_y,render_camera_z,render_camera_valid\n";
        profileRows.reserve(options.frameLimit);
    }
    kb::input::Win32XInputHapticsBackend hapticsBackend;
    kb::input::InputHaptics::RegisterBackend(scene, hapticsBackend);

    std::uint32_t renderedFrames = 0U;
    std::uint32_t submittedFrames = 0U;
    bool runtimeClean = true;
    kb::game::RuntimeSceneFrameSync renderSceneSync;
    auto previousTick = std::chrono::steady_clock::now();
    while (window.PumpMessages() && !scene.Runtime().ShouldQuit()) {
        if (window.Width() == 0U || window.Height() == 0U) {
            if (!options.profilePath.empty()) {
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
        const float deltaSeconds = options.profileFixedStep ? 1.0F / 60.0F : kb::game::RuntimeDeltaSeconds(previousTick, now);
        previousTick = now;

        if (!options.headless) inputCollector.Collect(scene.Input().MutableDeviceState(), window.Handle());
        static_cast<void>(scene.UI().SetViewport(
            static_cast<float>(window.Width()),
            static_cast<float>(window.Height())));
        renderSceneSync.BeforeUpdate(scene);
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
        for (const std::string& error : scene.Runtime().DrainSceneSystemErrors()) {
            std::cerr << "kb_game: scene runtime failed: " << error << '\n';
            runtimeClean = false;
        }
        if (scriptActive) {
            for (const std::string& error : scriptModule->Host()->DrainSceneSystemDiagnostics()) {
                std::cerr << "kb_game: script runtime failed: " << error << '\n';
                runtimeClean = false;
            }
        }
        if (!runtimeClean) {
            break;
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
                .transformMilliseconds = static_cast<double>(scene.Runtime().HotPathReport().runtimeTransformSyncNanoseconds) / 1e6,
                .sceneSyncMilliseconds = renderer.LastSceneSynchronizationMilliseconds(),
                .transformInspected = static_cast<std::uint32_t>(scene.Runtime().HotPathReport().transformHierarchyInspectedCount),
                .transformUpdated = static_cast<std::uint32_t>(scene.Runtime().HotPathReport().transformHierarchyUpdatedCount),
                .meshCommandReuseCount = renderStats.meshCommandReuseCount,
                .sceneEntities = static_cast<std::uint32_t>(scene.Entities().Count()),
                .streamingOperations = static_cast<std::uint32_t>(scene.LoadedContent().StreamingStats().operations),
                .streamingMilliseconds = scene.LoadedContent().StreamingStats().milliseconds,
                .renderCameraX = renderCamera.ray.origin.x,
                .renderCameraY = renderCamera.ray.origin.y,
                .renderCameraZ = renderCamera.ray.origin.z,
                .renderCameraValid = renderCamera.valid,
            });
        }
        if (!submitted || renderErrors) {
            std::cerr << "kb_game: renderer could not submit the scene cleanly; accepted=" << submitted
                      << " missing-resources=" << renderStats.HasMissingResources()
                      << " dropped-instances=" << renderStats.droppedInstanceCount << '\n';
            for (const auto& event : renderer.LastSceneDiagnostics().events) {
                if (event.severity == kb::render::SceneRenderDiagnosticSeverity::Error) {
                    std::cerr << "kb_game: render error kind=" << static_cast<unsigned>(event.kind)
                              << " entity=" << event.entityId << " mesh=" << event.meshAssetId
                              << " material=" << event.materialAssetId << " texture=" << event.textureAssetId
                              << " profile=" << event.postProcessProfileAssetId << '\n';
                }
            }
            runtimeClean = false;
            break;
        }
        ++submittedFrames;
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
                          << row.renderCameraValid << '\n';
        }
        profileOutput.flush();
        if (!profileOutput) {
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
              << " simulated=" << scene.Runtime().ElapsedSeconds() << '\n';
    std::cout.flush();
    return shutdownClean && runtimeClean ? EXIT_SUCCESS : EXIT_FAILURE;
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
