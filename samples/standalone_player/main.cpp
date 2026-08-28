#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetMigration.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "StandaloneInputRuntime.hpp"

#include <bgfx/bgfx.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint32_t kHeadlessWidth = 64U;
constexpr std::uint32_t kHeadlessHeight = 64U;

class StandaloneSurface final : public kb::render::RenderSurface {
public:
    ~StandaloneSurface() override {
        if (window_ != nullptr) {
            static_cast<void>(UnregisterTouchWindow(window_));
            DestroyWindow(window_);
        }
        if (windowClass_ != 0U) {
            UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
        }
    }

    [[nodiscard]] bool Initialize(
        bool visible, kb::input::Win32InputCollector& inputCollector) noexcept {
        inputCollector_ = &inputCollector;
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kWindowClassName;
        windowClass_ = RegisterClassExW(&windowClass);
        if (windowClass_ == 0U) {
            return false;
        }
        window_ = CreateWindowExW(
            0U,
            kWindowClassName,
            L"21kb Engine Standalone Runtime",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            640,
            360,
            nullptr,
            nullptr,
            windowClass.hInstance,
            this);
        if (window_ == nullptr) {
            return false;
        }
        if (RegisterTouchWindow(window_, 0U) == 0) {
            return false;
        }
        if (visible) {
            ShowWindow(window_, SW_SHOWNORMAL);
            UpdateWindow(window_);
        }
        return true;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return kHeadlessWidth;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return kHeadlessHeight;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] HWND Window() const noexcept {
        return window_;
    }

private:
    static LRESULT CALLBACK WindowProc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        StandaloneSurface* surface = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            surface = static_cast<StandaloneSurface*>(create->lpCreateParams);
            if (surface != nullptr) {
                SetWindowLongPtrW(
                    window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
            }
        } else {
            surface = reinterpret_cast<StandaloneSurface*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (surface != nullptr && surface->inputCollector_ != nullptr) {
            if (message == WM_SIZE) {
                const RECT viewport{
                    .left = 0,
                    .top = 0,
                    .right = static_cast<LONG>(LOWORD(lparam)),
                    .bottom = static_cast<LONG>(HIWORD(lparam)),
                };
                surface->inputCollector_->ConfigurePointerViewport(
                    window, viewport, kHeadlessWidth, kHeadlessHeight);
            }
            const bool handled = surface->inputCollector_->HandleWindowMessage(
                window, message, wparam, lparam);
            if (handled) {
                return 0;
            }
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    static constexpr const wchar_t* kWindowClassName = L"21kbStandaloneRuntimeWindow";
    ATOM windowClass_ = 0U;
    HWND window_ = nullptr;
    kb::input::Win32InputCollector* inputCollector_ = nullptr;
};

struct StandaloneOptions {
    std::filesystem::path graphCacheRoot;
    std::filesystem::path assetRoot;
    std::filesystem::path projectPath;
    std::string mountName = "Game";
    std::string meshPath = "/Game/triangle.obj";
    std::string materialPath = "/Game/graph.kbmat";
    std::string scenePath;
    std::optional<std::uint32_t> expectedGraphGpuCount{};
    bgfx::RendererType::Enum rendererType = bgfx::RendererType::Noop;
    bool selfTest = false;
    bool cameraRuntimeSelfTest = false;
    bool inputRuntimeTest = false;
    std::string focusProbeStopEvent;
    bool help = false;
};

[[nodiscard]] bool HasPrefix(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] std::string_view ValueAfter(std::string_view argument, std::string_view prefix) noexcept {
    return argument.substr(prefix.size());
}

[[nodiscard]] std::filesystem::path ExeDirectory() {
    std::wstring buffer;
    buffer.resize(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0U) {
            return std::filesystem::current_path();
        }
        if (written < buffer.size() - 1U) {
            buffer.resize(written);
            return std::filesystem::path{ buffer }.parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

[[nodiscard]] std::optional<std::uint32_t> ParseUInt(std::string_view text) noexcept {
    std::uint32_t value = 0U;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool ParseRenderer(std::string_view text, bgfx::RendererType::Enum& rendererType) noexcept {
    if (text == "noop") {
        rendererType = bgfx::RendererType::Noop;
        return true;
    }
    if (text == "d3d11") {
        rendererType = bgfx::RendererType::Direct3D11;
        return true;
    }
    return false;
}

void PrintUsage() {
    std::fprintf(stdout,
        "kb_standalone_player options:\n"
        "  --self-test\n"
        "  --camera-runtime-self-test\n"
        "  --input-runtime-test\n"
        "  --renderer=noop|d3d11\n"
        "  --project=<project directory or .21kbproject file>\n"
        "  --scene=<virtual or physical scene path; defaults to project defaultScene>\n"
        "  --graph-cache-root=<path>\n"
        "  --asset-root=<path>\n"
        "  --mount=<name>\n"
        "  --mesh=<virtual path>\n"
        "  --material=<virtual path>\n"
        "  --expect-graph-gpu-count=<count>\n");
    std::fflush(stdout);
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "v -0.1 -0.1 0.0\n"
        << "v 0.1 -0.1 0.0\n"
        << "v 0.0 0.1 0.0\n"
        << "vt 0 0\n"
        << "vt 1 0\n"
        << "vt 0.5 1\n"
        << "vn 0 0 1\n"
        << "f 1/1/1 2/2/1 3/3/1\n";
}

[[nodiscard]] kb::render::RenderMaterialAssetData MakeSelfTestGraphMaterial() {
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -160,
        .positionY = 64,
    });
    material.graph.nodes.back().parameter.defaultValueHint = "0.16 0.84 0.30 1";
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = 2U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::ConstantColor,
            "rgba",
            true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
            "baseColor",
            false),
        .toPin = "baseColor",
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    material.graph.links.push_back(link);
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -160,
        .positionY = 160,
    });
    material.graph.nodes.back().parameter.stableId = "roughnessOverride";
    material.graph.nodes.back().parameter.defaultValueHint = "0.2";
    kb::render::RenderMaterialGraphLink roughnessLink{
        .fromNodeId = 3U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
            "value",
            true),
        .fromPin = "value",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
            "roughness",
            false),
        .toPin = "roughness",
    };
    roughnessLink.id = kb::render::MakeRenderMaterialGraphLinkId(roughnessLink);
    material.graph.links.push_back(roughnessLink);
    return material;
}

void WriteLittleEndian16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void WriteLittleEndian32(std::ofstream& output, std::uint32_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
    output.put(static_cast<char>((value >> 16U) & 0xFFU));
    output.put(static_cast<char>((value >> 24U) & 0xFFU));
}

[[nodiscard]] bool WriteRuntimeSilentWav(const std::filesystem::path& path) {
    constexpr std::uint16_t channels = 1U;
    constexpr std::uint32_t sampleRate = 44'100U;
    constexpr std::uint16_t bitsPerSample = 16U;
    constexpr std::uint32_t sampleCount = sampleRate;
    constexpr std::uint32_t bytesPerSample = bitsPerSample / 8U;
    constexpr std::uint32_t dataSize =
        sampleCount * channels * bytesPerSample;
    constexpr std::uint32_t byteRate =
        sampleRate * channels * bytesPerSample;
    constexpr std::uint16_t blockAlign = channels * bytesPerSample;

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    output.write("RIFF", 4);
    WriteLittleEndian32(output, 36U + dataSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    WriteLittleEndian32(output, 16U);
    WriteLittleEndian16(output, 1U);
    WriteLittleEndian16(output, channels);
    WriteLittleEndian32(output, sampleRate);
    WriteLittleEndian32(output, byteRate);
    WriteLittleEndian16(output, blockAlign);
    WriteLittleEndian16(output, bitsPerSample);
    output.write("data", 4);
    WriteLittleEndian32(output, dataSize);
    constexpr std::array<char, 4096U> silence{};
    std::uint32_t remaining = dataSize;
    while (remaining > 0U) {
        const std::uint32_t chunk =
            std::min<std::uint32_t>(
                remaining, static_cast<std::uint32_t>(silence.size()));
        output.write(silence.data(), static_cast<std::streamsize>(chunk));
        remaining -= chunk;
    }
    return output.good();
}

[[nodiscard]] bool SaveSelfTestGraphMaterial(
    const std::filesystem::path& path) {
    return kb::render::RenderMaterialAssetWriter::Save(
        path, MakeSelfTestGraphMaterial());
}

[[nodiscard]] bool CookSelfTestGraphMaterial(
    const StandaloneOptions& options,
    std::string_view virtualPath,
    std::uint64_t assetId) {
#if defined(KB_STANDALONE_GRAPH_SHADERC_PATH)
    const kb::render::RenderMaterialAssetData material =
        MakeSelfTestGraphMaterial();
    const kb::render::RenderMaterialGraphCompileResult compiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            material.graph,
            kb::render::RenderMaterialGraphBuildContext{
                .assetId = assetId,
                .sourcePath = std::string{virtualPath}});
    if (!compiled.Succeeded()) {
        std::fprintf(stderr,
            "kb_standalone_player: self-test graph compile failed diagnostics=%zu\n",
            compiled.diagnostics.size());
        return false;
    }

    kb::render::RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_STANDALONE_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_STANDALONE_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = {
        KB_STANDALONE_GRAPH_SHADER_INCLUDE_DIR,
        KB_STANDALONE_GRAPH_BGFX_SHADER_INCLUDE_DIR};
    request.cacheRoot = options.graphCacheRoot.generic_string();
    const kb::render::RenderMaterialGraphShaderBackend backend =
        kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    constexpr std::array<std::string_view, 2> passes{
        "BaseOpaque",
        "ShadowDepth",
    };
    for (const std::string_view pass : passes) {
        request.pass = std::string{ pass };
        const kb::render::RenderMaterialGraphShaderArtifactResult cooked =
            kb::render::CookRenderMaterialGraphShaderArtifact(
            compiled.shader,
            std::span<const kb::render::RenderMaterialGraphShaderBackend>{
                &backend, 1U},
            request);
        if (cooked.Succeeded()) {
            continue;
        }
        std::fprintf(stderr,
            "kb_standalone_player: self-test graph cook failed pass=%s diagnostics=%zu\n",
            request.pass.c_str(),
            cooked.diagnostics.size());
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic :
             cooked.diagnostics) {
            std::fprintf(stderr,
                "kb_standalone_player: cook diagnostic: %s\n",
                diagnostic.message.c_str());
        }
        return false;
    }
    return true;
#else
    static_cast<void>(options);
    static_cast<void>(virtualPath);
    static_cast<void>(assetId);
    std::fprintf(stderr,
        "kb_standalone_player: self-test requires KB_STANDALONE_GRAPH_SHADERC_PATH\n");
    return false;
#endif
}

[[nodiscard]] bool PrepareGraphSelfTestPackage(StandaloneOptions& options, std::filesystem::path& packageRoot) {
#if defined(KB_STANDALONE_GRAPH_SHADERC_PATH)
    packageRoot = std::filesystem::temp_directory_path() /
        ("21kb_standalone_player_graph_self_test_" + std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())));
    std::error_code filesystemError;
    std::filesystem::remove_all(packageRoot, filesystemError);
    filesystemError.clear();
    std::filesystem::create_directories(packageRoot, filesystemError);
    if (filesystemError) {
        std::fprintf(stderr, "kb_standalone_player: self-test package directory failed: %s\n", packageRoot.string().c_str());
        return false;
    }

    WriteTriangleObj(packageRoot / "triangle.obj");
    if (!SaveSelfTestGraphMaterial(packageRoot / "graph.kbmat")) {
        std::fprintf(stderr, "kb_standalone_player: self-test graph material write failed\n");
        return false;
    }
    if (!CookSelfTestGraphMaterial(
            options, "/Game/graph.kbmat", 0x9900U)) {
        return false;
    }

    options.assetRoot = packageRoot;
    options.meshPath = "/Game/triangle.obj";
    options.materialPath = "/Game/graph.kbmat";
    options.expectedGraphGpuCount = 1U;
    std::fprintf(stdout, "kb_standalone_player: self_test_package=%s\n", packageRoot.string().c_str());
    std::fflush(stdout);
    return true;
#else
    static_cast<void>(options);
    static_cast<void>(packageRoot);
    std::fprintf(stderr, "kb_standalone_player: self-test requires KB_STANDALONE_GRAPH_SHADERC_PATH\n");
    return false;
#endif
}

[[nodiscard]] bool ParseOptions(int argc, char** argv, StandaloneOptions& options) {
    options.graphCacheRoot = ExeDirectory() / ".cache" / "graph_shaders";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--self-test") {
            options.selfTest = true;
        } else if (argument == "--camera-runtime-self-test") {
            options.cameraRuntimeSelfTest = true;
        } else if (argument == "--input-runtime-test") {
            options.inputRuntimeTest = true;
        } else if (HasPrefix(argument, "--focus-probe-stop-event=")) {
            options.focusProbeStopEvent =
                std::string{ValueAfter(
                    argument, "--focus-probe-stop-event=")};
        } else if (HasPrefix(argument, "--renderer=")) {
            const std::string_view rendererValue = ValueAfter(argument, "--renderer=");
            if (!ParseRenderer(rendererValue, options.rendererType)) {
                std::fprintf(stderr, "kb_standalone_player: unsupported renderer '%.*s'\n", static_cast<int>(rendererValue.size()), rendererValue.data());
                return false;
            }
        } else if (HasPrefix(argument, "--graph-cache-root=")) {
            options.graphCacheRoot = std::filesystem::path{ std::string{ ValueAfter(argument, "--graph-cache-root=") } };
        } else if (HasPrefix(argument, "--asset-root=")) {
            options.assetRoot = std::filesystem::path{ std::string{ ValueAfter(argument, "--asset-root=") } };
        } else if (HasPrefix(argument, "--project=")) {
            options.projectPath = std::filesystem::path{ std::string{ ValueAfter(argument, "--project=") } };
        } else if (HasPrefix(argument, "--scene=")) {
            options.scenePath = std::string{ ValueAfter(argument, "--scene=") };
        } else if (HasPrefix(argument, "--mount=")) {
            options.mountName = std::string{ ValueAfter(argument, "--mount=") };
        } else if (HasPrefix(argument, "--mesh=")) {
            options.meshPath = std::string{ ValueAfter(argument, "--mesh=") };
        } else if (HasPrefix(argument, "--material=")) {
            options.materialPath = std::string{ ValueAfter(argument, "--material=") };
        } else if (HasPrefix(argument, "--expect-graph-gpu-count=")) {
            options.expectedGraphGpuCount = ParseUInt(ValueAfter(argument, "--expect-graph-gpu-count="));
            if (!options.expectedGraphGpuCount.has_value()) {
                std::fprintf(stderr, "kb_standalone_player: invalid graph GPU count\n");
                return false;
            }
        } else {
            std::fprintf(stderr, "kb_standalone_player: unknown option '%.*s'\n", static_cast<int>(argument.size()), argument.data());
            return false;
        }
    }
    return true;
}

[[nodiscard]] kb::render::RenderSceneSubmitDesc HeadlessSubmitDesc(
    std::uint32_t viewportId = 1U,
    std::uint32_t viewportIndex = 0U,
    kb::input::LocalUserId localUser = kb::input::kPrimaryLocalUser) noexcept {
    return kb::render::RenderSceneSubmitDesc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .depthTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{viewportId},
                .extent = kb::render::RenderExtent{kHeadlessWidth, kHeadlessHeight},
                .viewportIndex = viewportIndex,
                .localUserId = localUser.value,
            },
        },
        .clearRgba = 0x101018FFU,
    };
}

[[nodiscard]] bool RegisterRuntimeAssetLoaders(kb::scene::Scene& scene) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>())) {
        std::fprintf(stderr, "kb_standalone_player: mesh loader registration failed\n");
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>())) {
        std::fprintf(stderr, "kb_standalone_player: material loader registration failed\n");
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>())) {
        std::fprintf(stderr, "kb_standalone_player: texture loader registration failed\n");
        return false;
    }
    if (!manager.RegisterLoader(
            std::make_unique<kb::render::PostProcessProfileAssetLoader>())) {
        std::fprintf(stderr,
            "kb_standalone_player: post-process profile loader registration failed\n");
        return false;
    }
    kb::render::InstallRuntimeMaterialParameterValidation(scene);
    return true;
}

[[nodiscard]] bool BuildSceneFromAssets(const StandaloneOptions& options, kb::scene::Scene& scene) {
    if (!RegisterRuntimeAssetLoaders(scene)) {
        return false;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (!manager.Mounts().Mount(options.mountName, options.assetRoot)) {
        std::fprintf(stderr, "kb_standalone_player: asset mount failed for '%s'\n", options.assetRoot.string().c_str());
        return false;
    }
    const std::size_t discovered = manager.DiscoverMountedAssets();
    if (discovered == 0U) {
        std::fprintf(stderr, "kb_standalone_player: no assets discovered under '%s'\n", options.assetRoot.string().c_str());
        return false;
    }

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath(options.meshPath);
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath(options.materialPath);
    if (meshMetadata == nullptr || materialMetadata == nullptr) {
        std::fprintf(stderr,
            "kb_standalone_player: required assets missing mesh='%s' material='%s' discovered=%zu\n",
            options.meshPath.c_str(),
            options.materialPath.c_str(),
            discovered);
        return false;
    }
    const kb::render::RenderMaterialAssetParseResult materialParse =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(materialMetadata->physicalPath, materialMetadata->id);
    if (!materialParse.Succeeded()) {
        for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : materialParse.diagnostics) {
            std::fprintf(stderr, "kb_standalone_player: material parse diagnostic line=%zu field=%s message=%s\n",
                diagnostic.line,
                diagnostic.field.c_str(),
                diagnostic.message.c_str());
        }
    } else {
        const std::vector<kb::render::RenderMaterialGraphDiagnostic> graphDiagnostics =
            kb::render::ValidateRenderMaterialAssetGraphDiagnostics(*materialParse.asset);
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
                std::fprintf(stderr, "kb_standalone_player: material graph diagnostic node=%u pin=%s message=%s\n",
                    diagnostic.nodeId,
                    diagnostic.pin.c_str(),
                    diagnostic.message.c_str());
            }
        }
    }

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Standalone Graph Material Mesh",
        .transform = kb::scene::TransformComponent{
            .localScale = kb::scene::Vec3{1.0F, 1.0F, 1.0F},
            .worldScale = kb::scene::Vec3{1.0F, 1.0F, 1.0F},
            .worldDirty = false,
        },
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialMetadata->id.value,
    });
    const kb::scene::SceneObject camera =
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Standalone Runtime Camera",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, -2.0F },
            },
        });
    scene.Components().Cameras().Set(camera.Entity(), kb::scene::CameraComponent{
        .primary = true,
        .viewportId = 1U,
    });
    return true;
}

struct StandaloneProjectRuntimeConfig {
    kb::project::ProjectDescriptor descriptor{};
    std::filesystem::path projectRoot;
    std::string sceneReference;
    std::string physicsLayersAsset;
    std::string inputMappingContext;
    bool inputEnabled = true;
    std::vector<std::string> requiredModules;
};

[[nodiscard]] bool ReadProjectRuntimeConfig(
    const StandaloneOptions& options,
    StandaloneProjectRuntimeConfig& config) {
    std::error_code pathError;
    const std::filesystem::path absoluteInput =
        std::filesystem::absolute(options.projectPath, pathError).lexically_normal();
    if (pathError) {
        std::fprintf(stderr,
            "kb_standalone_player: project path could not be resolved: %s\n",
            options.projectPath.string().c_str());
        return false;
    }

    std::filesystem::path projectFile = absoluteInput;
    if (std::filesystem::is_directory(absoluteInput, pathError) && !pathError) {
        projectFile /= "Project.21kbproject";
    }
    if (pathError ||
        !std::filesystem::is_regular_file(projectFile, pathError) ||
        pathError) {
        std::fprintf(stderr,
            "kb_standalone_player: project descriptor was not found: %s\n",
            projectFile.string().c_str());
        return false;
    }

    kb::project::ProjectDescriptorReadResult loaded =
        kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        std::fprintf(stderr,
            "kb_standalone_player: project descriptor load failed: %s\n",
            loaded.error.c_str());
        return false;
    }

    const kb::project::ParticleProjectPolicyResult particlePolicy =
        kb::project::ParticleProjectPolicy::Inspect(projectFile.parent_path(), loaded.descriptor);
    if (!particlePolicy.IsRunnable()) {
        std::fprintf(stderr, "kb_standalone_player: %s\n", particlePolicy.diagnostic.c_str());
        return false;
    }

    config.projectRoot = projectFile.parent_path();
    for (kb::project::ProjectPluginReference& plugin : loaded.descriptor.plugins) {
        if (!plugin.enabled) {
            continue;
        }
        if (!plugin.name.empty()) {
            config.requiredModules.push_back(plugin.name);
        }
        const std::filesystem::path configuredPath{ plugin.binaryPath };
        if (configuredPath.empty() || configuredPath.is_absolute()) {
            continue;
        }
        const std::filesystem::path projectLocalPath =
            config.projectRoot / configuredPath;
        std::error_code pluginError;
        if (std::filesystem::is_regular_file(projectLocalPath, pluginError) &&
            !pluginError) {
            plugin.binaryPath = projectLocalPath.string();
        }
    }

    // The game reads the same settings file the editor writes, so shipping a change
    // means editing one file rather than rebuilding the project descriptor.
    const kb::project::ProjectSettingsLoadResult settings =
        kb::project::ProjectSettingsStore::Load(
            kb::project::ProjectSettingsStore::FilePath(config.projectRoot));
    if (!settings.Succeeded()) {
        std::fprintf(stderr, "Project settings could not be read: %s\n", settings.error.c_str());
        return false;
    }
    // A package built before the settings file existed still carries its settings in
    // the descriptor, so it keeps running rather than starting with no scene.
    const kb::project::ProjectSettings resolved = settings.found
        ? settings.settings
        : kb::project::ProjectSettingsStore::FromLegacy(loaded.legacySettings, projectFile);
    config.sceneReference =
        options.scenePath.empty() ? resolved.defaultMap : options.scenePath;
    config.physicsLayersAsset = resolved.physicsLayersAsset;
    config.inputMappingContext = resolved.inputMappingContext;
    config.inputEnabled = resolved.inputEnabled;
    config.descriptor = std::move(loaded.descriptor);
    return true;
}

[[nodiscard]] bool LoadProjectScene(
    const StandaloneProjectRuntimeConfig& config,
    kb::scene::Scene& scene) {
    if (!scene.ModuleDiagnostics().empty()) {
        for (const std::string& diagnostic : scene.ModuleDiagnostics()) {
            std::fprintf(stderr,
                "kb_standalone_player: module diagnostic: %s\n",
                diagnostic.c_str());
        }
        return false;
    }
    for (const std::string& module : config.requiredModules) {
        if (!scene.IsModuleActive(module)) {
            std::fprintf(stderr,
                "kb_standalone_player: configured module is not active: %s\n",
                module.c_str());
            return false;
        }
    }
    if (!RegisterRuntimeAssetLoaders(scene)) {
        return false;
    }
    if (!scene.Assets().MountProject(config.projectRoot)) {
        std::fprintf(stderr,
            "kb_standalone_player: project assets could not be mounted: %s\n",
            config.projectRoot.string().c_str());
        return false;
    }
    const std::size_t discovered = scene.Assets().Discover();
    if (!config.physicsLayersAsset.empty() &&
        !kb::scene::PhysicsBackend::LoadAndConfigureLayers(
            scene, config.physicsLayersAsset)) {
        std::fprintf(stderr,
            "kb_standalone_player: project physics layers could not be applied: %s\n",
            config.physicsLayersAsset.c_str());
        return false;
    }

    std::filesystem::path scenePath;
    if (!config.sceneReference.empty() && config.sceneReference.front() == '/') {
        const kb::assets::AssetMetadata* metadata =
            scene.Assets().Manager().Registry().FindByPath(config.sceneReference);
        if (metadata == nullptr) {
            std::fprintf(stderr,
                "kb_standalone_player: project scene asset was not found: %s\n",
                config.sceneReference.c_str());
            return false;
        }
        scenePath = metadata->physicalPath;
    } else {
        scenePath = std::filesystem::path{ config.sceneReference };
        if (scenePath.is_relative()) {
            scenePath = config.projectRoot / scenePath;
        }
    }
    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath)) {
        std::fprintf(stderr,
            "kb_standalone_player: project scene could not be loaded: %s\n",
            scenePath.string().c_str());
        return false;
    }

    kb::scene::SceneInputActivation::Apply(scene);
    if (config.inputEnabled && !config.inputMappingContext.empty()) {
        const kb::assets::AssetMetadata* input =
            scene.Assets().Manager().Registry().FindByPath(config.inputMappingContext);
        if (input == nullptr ||
            input->type != "InputMappingContext" ||
            !scene.Input().AddMappingContext(input->id.value, 0)) {
            std::fprintf(stderr,
                "kb_standalone_player: project input mapping could not be activated: %s\n",
                config.inputMappingContext.c_str());
            return false;
        }
    }

    std::fprintf(stdout,
        "kb_standalone_player: project=%s scene=%s assets=%zu modules=%zu\n",
        config.projectRoot.string().c_str(),
        scenePath.string().c_str(),
        discovered,
        scene.ActiveModuleCount());
    std::fflush(stdout);
    return true;
}

[[nodiscard]] bool PrepareCameraRuntimeSelfTestProject(
    StandaloneOptions& options,
    std::filesystem::path& packageRoot) {
    packageRoot = std::filesystem::temp_directory_path() /
        ("21kb_camera_runtime_self_test_" +
            std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())));
    std::error_code filesystemError;
    std::filesystem::remove_all(packageRoot, filesystemError);
    filesystemError.clear();
    std::filesystem::create_directories(
        packageRoot / "Assets" / "Scenes", filesystemError);
    if (filesystemError) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime package directory failed\n");
        return false;
    }
    const std::filesystem::path capturePath =
        packageRoot / "runtime_capture.png";
    std::string luaCapturePath;
    for (const char character : capturePath.generic_string()) {
        if (character == '\\' || character == '\'') {
            luaCapturePath.push_back('\\');
        }
        luaCapturePath.push_back(character);
    }
    if (!WriteRuntimeSilentWav(
            packageRoot / "Assets" / "runtime.wav")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime audio clip write failed\n");
        return false;
    }
    const kb::audio::AudioMixerAsset audioMixer{
        .buses = {
            kb::audio::AudioMixerBus{
                .name = "Effects",
                .volume = 0.8F},
            kb::audio::AudioMixerBus{
                .name = "World",
                .parentBus = "Effects",
                .volume = 0.6F},
        },
        .snapshots = {
            kb::audio::AudioMixerSnapshot{
                .name = "Focused",
                .busVolumes = {
                    kb::audio::AudioMixerSnapshotBusVolume{
                        .bus = "Effects",
                        .volume = 0.5F},
                }},
            kb::audio::AudioMixerSnapshot{
                .name = "Quiet",
                .busVolumes = {
                    kb::audio::AudioMixerSnapshotBusVolume{
                        .bus = "Effects",
                        .volume = 0.2F},
                    kb::audio::AudioMixerSnapshotBusVolume{
                        .bus = "World",
                        .volume = 0.1F},
                }},
        }};
    if (!kb::audio::AudioMixerAssetIO::Save(
            packageRoot / "Assets" / "runtime.kbmixer",
            audioMixer)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime audio mixer write failed\n");
        return false;
    }
    WriteTriangleObj(packageRoot / "Assets" / "triangle.obj");
    if (!SaveSelfTestGraphMaterial(
            packageRoot / "Assets" / "graph.kbmat")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime material write failed\n");
        return false;
    }
    kb::render::ScenePostProcessSettings postProcessProfile{};
    postProcessProfile.bloomEnabled = true;
    postProcessProfile.bloomStrength = 0.37F;
    postProcessProfile.fxaaEnabled = true;
    postProcessProfile.temporalAntiAliasingEnabled = false;
    postProcessProfile.outputTransform.exposureStops = 0.75F;
    if (!kb::render::PostProcessProfileAssetLoader::SaveProfile(
            packageRoot / "Assets" / "runtime.kbppfx",
            postProcessProfile)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime post-process profile write failed\n");
        return false;
    }
    kb::scene::LegacyParticleEffectAsset legacyParticleEffect{};
    legacyParticleEffect.materialReference = "/Game/graph.kbmat";
    legacyParticleEffect.looping = true;
    legacyParticleEffect.maxParticles = 8U;
    legacyParticleEffect.emissionRatePerSecond = 0.0F;
    legacyParticleEffect.startSpeedMin = 0.5F;
    legacyParticleEffect.startSpeedMax = 0.5F;
    legacyParticleEffect.startLifetimeMin = 2.0F;
    legacyParticleEffect.startLifetimeMax = 2.0F;
    legacyParticleEffect.spreadDegrees = 0.0F;
    const kb::scene::ParticleEffectAsset particleEffect =
        kb::scene::ParticleEffectAssetMigration::FromLegacy(legacyParticleEffect);
    if (!kb::scene::ParticleEffectAssetIO::Save(
            packageRoot / "Assets" / "runtime.kbvfx",
            particleEffect)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime particle effect write failed\n");
        return false;
    }
    kb::scene::LegacyParticleEffectAsset legacyCompletionEffect = legacyParticleEffect;
    legacyCompletionEffect.looping = false;
    legacyCompletionEffect.durationSeconds = 0.03F;
    legacyCompletionEffect.emissionRatePerSecond = 120.0F;
    legacyCompletionEffect.startLifetimeMin = 0.01F;
    legacyCompletionEffect.startLifetimeMax = 0.01F;
    const kb::scene::ParticleEffectAsset completionEffect =
        kb::scene::ParticleEffectAssetMigration::FromLegacy(legacyCompletionEffect);
    if (!kb::scene::ParticleEffectAssetIO::Save(
            packageRoot / "Assets" / "runtime_completion.kbvfx",
            completionEffect)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime completion particle effect write failed\n");
        return false;
    }
    {
        std::ofstream script{
            packageRoot / "Assets" / "MeshRuntime.lua",
            std::ios::trunc};
        script
            << "function Tick(self, dt)\n"
            << "    if runtimeInitialized then\n"
            << "        SetShared('runtimeHasRenderFrame', Renderer.HasFrame())\n"
            << "        SetShared('runtimeIsVisible', Renderer.IsVisible())\n"
            << "        local bounds = Renderer.GetBounds()\n"
            << "        SetShared('runtimeBoundsFound', bounds.found)\n"
            << "        SetShared('runtimeBoundsRadius', bounds.radius)\n"
            << "        local projected = Renderer.WorldToScreen(6.0, 2.0, -2.0)\n"
            << "        SetShared('runtimeProjectedValid', projected.valid and projected.onScreen)\n"
            << "        SetShared('runtimeProjectedX', projected.screenX)\n"
            << "        SetShared('runtimeProjectedY', projected.screenY)\n"
            << "        local ray = Renderer.ScreenPointToRay(32.0, 32.0)\n"
            << "        SetShared('runtimeRayValid', ray.valid)\n"
            << "        SetShared('runtimeRayOriginZ', ray.originZ)\n"
            << "        SetShared('runtimeRayDirectionZ', ray.directionZ)\n"
            << "        local world = Renderer.ScreenToWorld(32.0, 32.0, 2.0)\n"
            << "        SetShared('runtimeScreenToWorldValid', world.valid)\n"
            << "        SetShared('runtimeScreenToWorldZ', world.z)\n"
            << "        if not runtimeCapture then\n"
            << "            runtimeCapture = Renderer.CaptureScreen('" << luaCapturePath << "')\n"
            << "        end\n"
            << "        SetShared('runtimeCaptureStatus', Renderer.CaptureStatus(runtimeCapture))\n"
            << "        if not runtimeAudioReady then\n"
            << "            runtimeAudioReady = true\n"
            << "        elseif not runtimeAudioVoice then\n"
            << "            local audioError\n"
            << "            runtimeAudioVoice, audioError = Audio.Play('/Game/runtime.wav', { outputBus = 'Effects', loop = true, spatial = false, priority = 220 })\n"
            << "            SetShared('runtimeAudioError', audioError or '')\n"
            << "            SetShared('runtimeAudioPaused', Audio.Pause(runtimeAudioVoice))\n"
            << "            SetShared('runtimeAudioPausedState', not Audio.IsPlaying(runtimeAudioVoice))\n"
            << "            SetShared('runtimeAudioResumed', Audio.Resume(runtimeAudioVoice))\n"
            << "            SetShared('runtimeAudioSeeked', Audio.Seek(runtimeAudioVoice, 0.25))\n"
            << "            SetShared('runtimeAudioVolumeSet', Audio.SetVolume(runtimeAudioVoice, 0.4))\n"
            << "            SetShared('runtimeAudioPitchSet', Audio.SetPitch(runtimeAudioVoice, 1.1))\n"
            << "            SetShared('runtimeAudioLoopSet', Audio.SetLoop(runtimeAudioVoice, true))\n"
            << "            local audioPosition = Audio.GetPosition(runtimeAudioVoice)\n"
            << "            SetShared('runtimeAudioPositionValid', audioPosition.valid)\n"
            << "            SetShared('runtimeAudioPositionSeconds', audioPosition.seconds)\n"
            << "            SetShared('runtimeAudioMarkerAdded', Audio.AddMarker(runtimeAudioVoice, 'GameplayBeat', 0.0))\n"
            << "            local stoppedVoice = Audio.Play('/Game/runtime.wav', { outputBus = 'World', loop = true, spatialBlend = 0.25, priority = 10 })\n"
            << "            SetShared('runtimeAudioStopped', Audio.Stop(stoppedVoice))\n"
            << "            SetShared('runtimeAudioStoppedState', not Audio.IsPlaying(stoppedVoice))\n"
            << "            SetShared('runtimeAudioBusSet', Audio.SetBusVolume('Effects', 0.65))\n"
            << "            SetShared('runtimeAudioBusCleared', Audio.ClearBusVolume('Effects'))\n"
            << "            SetShared('runtimeAudioBusClearIdempotent', not Audio.ClearBusVolume('Effects'))\n"
            << "            SetShared('runtimeAudioBusFinalSet', Audio.SetBusVolume('Effects', 0.55))\n"
            << "            SetShared('runtimeAudioTransitionStarted', Audio.TransitionToSnapshot('Quiet', 0.05))\n"
            << "            local audioOwner = World.FindByName('Runtime Audio Owner')\n"
            << "            runtimeAttachedVoice = Audio.Play('/Game/runtime.wav', { entity = audioOwner, attach = true, loop = true, spatial = true, priority = 180 })\n"
            << "            SetShared('runtimeAttachedVoice', runtimeAttachedVoice)\n"
            << "        end\n"
            << "        SetShared('runtimeAudioVoice', runtimeAudioVoice)\n"
            << "        return\n"
            << "    end\n"
            << "    runtimeInitialized = true\n"
            << "    MeshRenderer.SetMesh('/Game/triangle.obj', self.entity)\n"
            << "    MeshRenderer.SetMaterial('/Game/graph.kbmat', self.entity)\n"
            << "    MeshRenderer.SetMaterialSlot(0, '/Game/graph.kbmat', self.entity)\n"
            << "    local instance = MaterialInstance.Create('/Game/graph.kbmat')\n"
            << "    MaterialInstance.SetParameterScalar(instance, 'roughnessOverride', 0.73)\n"
            << "    MeshRenderer.SetMaterialInstance(instance, self.entity)\n"
            << "    Audio.SetMixer('/Game/runtime.kbmixer')\n"
            << "    Audio.SetSnapshot('Focused')\n"
            << "    SetShared('runtimeOcclusionConfigured', Audio.ConfigureOcclusion(true, { occludedVolume = 0.2, maxDistance = 50.0, layerMask = 2147483647, maxRaycastsPerTick = 1 }))\n"
            << "    SetShared('runtimeOcclusionEnabled', Audio.OcclusionEnabled())\n"
            << "    local haptics = Input.HasHaptics(0)\n"
            << "    local hapticsOutOfRange = Input.HasHaptics(4)\n"
            << "    local vibrationApplied = Input.SetVibration(0, 0.01, 0.01)\n"
            << "    local userHaptics = Input.HasHaptics(1)\n"
            << "    local userHapticsBound = Input.BindHapticsUser(2, 1)\n"
            << "    local userVibrationApplied = Input.SetUserVibration(2, 0.01, 0.01)\n"
            << "    SetShared('runtimeHapticsSupported', haptics.supported)\n"
            << "    SetShared('runtimeHapticsConnected', haptics.connected)\n"
            << "    SetShared('runtimeHapticsDualMotor', haptics.dualMotor)\n"
            << "    SetShared('runtimeHapticsMaxGamepads', haptics.maxGamepads)\n"
            << "    SetShared('runtimeHapticsLimitRejected', not hapticsOutOfRange.supported and hapticsOutOfRange.maxGamepads == 4 and hapticsOutOfRange.reason ~= '')\n"
            << "    SetShared('runtimeHapticsConnectionHonest', vibrationApplied == haptics.connected)\n"
            << "    SetShared('runtimeHapticsUserRouteValid', userHapticsBound and userVibrationApplied == userHaptics.connected)\n"
            << "    SetShared('runtimeHapticsStopped', Input.StopVibration())\n"
            << "    PostProcess.SetProfile('/Game/runtime.kbppfx')\n"
            << "    local particles = Particles.Create('/Game/runtime.kbvfx', { entity = self.entity })\n"
            << "    Particles.SetSeed(particles, 12345)\n"
            << "    Particles.SetParameterScalar(particles, 'gravityScale', 0.0)\n"
            << "    Particles.Play(particles)\n"
            << "    Particles.Emit(particles, 2)\n"
            << "    local completionParticles = Particles.Create('/Game/runtime_completion.kbvfx', { entity = self.entity })\n"
            << "    Particles.Play(completionParticles)\n"
            << "end\n"
            << "function OnParticleSystemFinished(self, event)\n"
            << "    SetShared('runtimeParticleFinished', event.args.instance ~= nil and event.args.effect ~= nil)\n"
            << "    Particles.Release(event.args.instance)\n"
            << "end\n"
            << "function OnAudioMarker(self, event)\n"
            << "    SetShared('runtimeAudioMarkerReceived', event.args.marker == 'GameplayBeat')\n"
            << "    SetShared('runtimeAudioMarkerVoice', event.args.voice)\n"
            << "    SetShared('runtimeAudioMarkerPosition', event.args.positionSeconds)\n"
            << "end\n";
        if (!script) {
            std::fprintf(stderr,
                "kb_standalone_player: camera runtime Lua write failed\n");
            return false;
        }
    }

    kb::project::ProjectDescriptor descriptor{};
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Rendering.BasicLighting",
        .binaryPath = (
            ExeDirectory().parent_path().parent_path() /
            "basic_lighting" / "Debug" / "kb_basic_lighting_plugin.dll").string(),
        .enabled = true,
    });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Audio.Miniaudio",
        .binaryPath = (
            ExeDirectory().parent_path().parent_path() /
            "audio_miniaudio" / "Debug" /
            "kb_audio_miniaudio_plugin.dll").string(),
        .enabled = true,
    });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Rendering.21kbParticle",
        .binaryPath = "kb_21kb_particle_plugin.dll",
        .enabled = true,
    });
    if (!kb::project::ProjectManager::SaveProject(
            packageRoot / "Project.21kbproject", descriptor)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime project descriptor write failed\n");
        return false;
    }

    // A packaged game reads its settings file, so the self test ships one rather
    // than leaning on the descriptor fallback kept for older packages.
    {
        kb::project::ProjectSettings selfTestSettings;
        selfTestSettings.name = "CameraRuntimeSelfTest";
        selfTestSettings.gameName = selfTestSettings.name;
        selfTestSettings.defaultMap = "/Game/Scenes/CameraRuntime.21kbscene";
        std::string settingsError;
        if (!kb::project::ProjectSettingsStore::Save(
                kb::project::ProjectSettingsStore::FilePath(packageRoot),
                selfTestSettings,
                settingsError)) {
            std::fprintf(stderr,
                "kb_standalone_player: camera runtime project settings write failed: %s\n",
                settingsError.c_str());
            return false;
        }
    }

    kb::scene::Scene authoringScene;
    if (!RegisterRuntimeAssetLoaders(authoringScene) ||
        !authoringScene.Assets().MountProject(packageRoot) ||
        authoringScene.Assets().Discover() != 8U) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime asset discovery failed\n");
        return false;
    }
    const kb::assets::AssetMetadata* meshMetadata =
        authoringScene.Assets().Manager().Registry().FindByPath(
            "/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata =
        authoringScene.Assets().Manager().Registry().FindByPath(
            "/Game/graph.kbmat");
    const kb::assets::AssetMetadata* scriptMetadata =
        authoringScene.Assets().Manager().Registry().FindByPath(
            "/Game/MeshRuntime.lua");
    const kb::assets::AssetMetadata* audioClipMetadata =
        authoringScene.Assets().Manager().Registry().FindByPath(
            "/Game/runtime.wav");
    const kb::assets::AssetMetadata* audioMixerMetadata =
        authoringScene.Assets().Manager().Registry().FindByPath(
            "/Game/runtime.kbmixer");
    if (meshMetadata == nullptr || materialMetadata == nullptr ||
        scriptMetadata == nullptr || audioClipMetadata == nullptr ||
        audioMixerMetadata == nullptr ||
        !CookSelfTestGraphMaterial(
            options, "/Game/graph.kbmat", materialMetadata->id.value)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime asset preparation failed\n");
        return false;
    }
    const std::uint64_t expectedMeshAssetId = meshMetadata->id.value;
    const std::uint64_t expectedMaterialAssetId = materialMetadata->id.value;
    const std::uint64_t scriptAssetId = scriptMetadata->id.value;
    const kb::scene::SceneObject fallbackCamera =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Fallback Camera",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ -6.0F, 0.0F, -4.0F },
            },
        });
    authoringScene.Components().Cameras().Set(
        fallbackCamera.Entity(),
        kb::scene::CameraComponent{
            .projection = kb::scene::CameraProjection::Perspective,
            .verticalFovDegrees = 73.0F,
            .nearClip = 0.1F,
            .farClip = 500.0F,
            .primary = true,
            .viewportId = 0U,
            .priority = 10,
        });

    const kb::scene::SceneObject selectedCamera =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Selected Camera",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 6.0F, 2.0F, -4.0F },
            },
        });
    authoringScene.Components().Cameras().Set(
        selectedCamera.Entity(),
        kb::scene::CameraComponent{
            .projection = kb::scene::CameraProjection::Perspective,
            .verticalFovDegrees = 37.0F,
            .orthographicHeight = 14.0F,
            .nearClip = 0.25F,
            .farClip = 321.0F,
            .primary = true,
            .viewportId = 1U,
            .priority = 100,
            .cullingMask = 0x00000001U,
            .clearMode = kb::scene::CameraClearMode::DepthOnly,
            .clearColor = kb::scene::Vec3{0.25F, 0.5F, 0.75F},
        });
    authoringScene.Components().AudioListeners().Set(
        selectedCamera.Entity(),
        kb::scene::AudioListenerComponent{
            .primary = true,
            .enabled = true});

    const kb::scene::SceneObject secondaryPlayerCamera =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Secondary Player Camera",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{-8.0F, 3.0F, -5.0F},
            },
        });
    authoringScene.Components().Cameras().Set(
        secondaryPlayerCamera.Entity(),
        kb::scene::CameraComponent{
            .projection = kb::scene::CameraProjection::Perspective,
            .verticalFovDegrees = 51.0F,
            .nearClip = 0.5F,
            .farClip = 222.0F,
            .primary = true,
            .viewportId = 2U,
            .priority = 200,
            .cullingMask = 0x00000002U,
            .clearMode = kb::scene::CameraClearMode::SolidColor,
            .clearColor = kb::scene::Vec3{0.75F, 0.25F, 0.5F},
        });
    const kb::scene::SceneObject runtimeLight =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Runtime Key Light",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{0.0F, 8.0F, -4.0F},
            },
        });
    static_cast<void>(
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Runtime Audio Owner",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{2.0F, 1.0F, 0.0F},
            },
        }));
    const kb::scene::SceneObject audioOccluder =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Runtime Audio Occluder",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{6.0F, 2.0F, -2.0F},
            },
        });
    authoringScene.Components().Colliders().Set(
        audioOccluder.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{1.0F, 3.0F, 1.0F},
            .layer = 0x00000001U});
    authoringScene.Components().Lights().Set(
        runtimeLight.Entity(),
        kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Directional,
            .color = kb::scene::Vec3{1.0F, 0.9F, 0.8F},
            .intensity = 3.25F,
            .castsShadow = true,
            .useColorTemperature = true,
            .colorTemperatureKelvin = 3200.0F,
            .layerMask = 0x00000001U,
        });
    const kb::scene::SceneObject scriptedMesh =
        authoringScene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Scripted Mesh",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{6.0F, 2.0F, 0.0F},
            },
        });
    authoringScene.Components().Behaviours().Set(
        scriptedMesh.Entity(),
        kb::scene::BehaviourComponent{
            .behaviourAssetId = scriptAssetId,
            .backend = kb::scene::BehaviourBackend::Lua,
            .enabled = true,
        });
    kb::scene::AudioSourceComponent audioSource{
        .clipAssetId = audioClipMetadata->id.value,
        .volume = 0.7F,
        .pitch = 1.0F,
        .loop = true,
        .spatial = true,
        .autoplay = true,
        .enabled = true};
    if (!kb::scene::SetAudioSourceOutputBus(audioSource, "World")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime audio bus is invalid\n");
        return false;
    }

    authoringScene.Components().AudioSources().Set(
        scriptedMesh.Entity(), audioSource);
    if (!kb::scene::SceneDocumentService::Save(
            authoringScene,
            packageRoot / "Assets" / "Scenes" / "CameraRuntime.21kbscene",
            "CameraRuntime")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime scene write failed\n");
        return false;
    }

    options.projectPath = packageRoot;
    options.expectedGraphGpuCount = 1U;
    std::fprintf(stdout,
        "kb_standalone_player: camera_runtime_package=%s mesh=%llx material=%llx\n",
        packageRoot.string().c_str(),
        static_cast<unsigned long long>(expectedMeshAssetId),
        static_cast<unsigned long long>(expectedMaterialAssetId));
    std::fflush(stdout);
    return true;
}

[[nodiscard]] bool ValidateProjectCameraRuntime(
    kb::scene::Scene& scene,
    const kb::render::Renderer& renderer,
    const kb::script::ScriptRuntimeHost* scriptHost,
    const std::filesystem::path& capturePath,
    const bool expectsUnsupportedNoopCapture) {
    const kb::scene::SceneRenderCameraRay ray =
        kb::scene::SceneRenderFeedback::ScreenPointToRay(
            scene, kb::input::kPrimaryLocalUser, 32.0F, 32.0F);
    const kb::scene::SceneRenderCameraRay secondaryRay =
        kb::scene::SceneRenderFeedback::ScreenPointToRay(
            scene, kb::input::LocalUserId{2U}, 32.0F, 32.0F);
    const bool selectedPoseReachedRenderer =
        ray.valid &&
        std::fabs(ray.ray.origin.x - 6.0F) < 0.01F &&
        std::fabs(ray.ray.origin.y - 2.0F) < 0.01F &&
        std::fabs(ray.ray.origin.z + 4.0F) < 0.01F;

    bool serializedFieldsLoaded = false;
    bool scriptedMeshAssigned = false;
    bool scriptedMaterialSlotAssigned = false;
    bool scriptedMaterialInstanceAssigned = false;
    bool scriptedMaterialParameterApplied = false;
    bool serializedLightLoaded = false;
    bool serializedAudioSourceLoaded = false;
    bool serializedAudioListenerLoaded = false;
    std::uint64_t runtimeLightEntityId = 0U;
    for (const kb::scene::SceneEntity root : scene.Hierarchy().RootEntities()) {
        if (scene.Entities().Name(root) == "Scripted Mesh") {
            const kb::scene::MeshRendererComponent* meshRenderer =
                scene.Components().MeshRenderers().TryGet(root);
            scriptedMeshAssigned =
                meshRenderer != nullptr &&
                meshRenderer->meshAssetId != 0U &&
                meshRenderer->materialAssetId != 0U;
            scriptedMaterialSlotAssigned =
                meshRenderer != nullptr &&
                meshRenderer->materialSlotOverrideCount == 1U &&
                meshRenderer->materialSlotAssetIds[0] ==
                    meshRenderer->materialAssetId;
            scriptedMaterialInstanceAssigned =
                meshRenderer != nullptr &&
                meshRenderer->materialInstanceHandle != 0U &&
                scene.MaterialInstances().Exists(
                    meshRenderer->materialInstanceHandle) &&
                scene.MaterialInstances().Parent(
                    meshRenderer->materialInstanceHandle) ==
                    meshRenderer->materialAssetId;
            if (scriptedMaterialInstanceAssigned) {
                const auto parameters = scene.MaterialInstances().Parameters(
                    meshRenderer->materialInstanceHandle);
                scriptedMaterialParameterApplied =
                    parameters.size() == 1U &&
                    parameters.front().name == "roughnessOverride" &&
                    parameters.front().type ==
                        kb::scene::MaterialParameterType::Scalar &&
                    std::fabs(parameters.front().scalarValue - 0.73F) < 0.001F;
            }
            const kb::scene::AudioSourceComponent* audioSource =
                scene.Components().AudioSources().TryGet(root);
            serializedAudioSourceLoaded =
                audioSource != nullptr &&
                audioSource->clipAssetId != 0U &&
                audioSource->loop &&
                audioSource->spatial &&
                audioSource->autoplay &&
                kb::scene::AudioSourceOutputBus(*audioSource) == "World";
        }
        if (scene.Entities().Name(root) == "Selected Camera") {
            const kb::scene::CameraComponent* camera =
                scene.Components().Cameras().TryGet(root);
            serializedFieldsLoaded =
                camera != nullptr &&
                camera->projection ==
                    kb::scene::CameraProjection::Perspective &&
                std::fabs(camera->verticalFovDegrees - 37.0F) < 0.001F &&
                std::fabs(camera->orthographicHeight - 14.0F) < 0.001F &&
                std::fabs(camera->nearClip - 0.25F) < 0.001F &&
                std::fabs(camera->farClip - 321.0F) < 0.001F &&
                camera->primary &&
                camera->viewportId == 1U &&
                camera->priority == 100 &&
                camera->cullingMask == 0x00000001U &&
                camera->clearMode ==
                    kb::scene::CameraClearMode::DepthOnly &&
                std::fabs(camera->clearColor.x - 0.25F) < 0.001F &&
                std::fabs(camera->clearColor.y - 0.5F) < 0.001F &&
                std::fabs(camera->clearColor.z - 0.75F) < 0.001F;
            const kb::scene::AudioListenerComponent* listener =
                scene.Components().AudioListeners().TryGet(root);
            serializedAudioListenerLoaded =
                listener != nullptr && listener->primary && listener->enabled;
        }
        if (scene.Entities().Name(root) == "Runtime Key Light") {
            const kb::scene::LightComponent* light =
                scene.Components().Lights().TryGet(root);
            serializedLightLoaded =
                light != nullptr &&
                light->kind == kb::scene::LightKind::Directional &&
                std::fabs(light->intensity - 3.25F) < 0.001F &&
                light->castsShadow &&
                light->useColorTemperature &&
                std::fabs(light->colorTemperatureKelvin - 3200.0F) < 0.001F &&
                light->layerMask == 0x00000001U;
            runtimeLightEntityId = root.Id();
        }
    }

    const bool secondaryPlayerPoseReachedRenderer =
        secondaryRay.valid &&
        std::fabs(secondaryRay.ray.origin.x + 8.0F) < 0.01F &&
        std::fabs(secondaryRay.ray.origin.y - 3.0F) < 0.01F &&
        std::fabs(secondaryRay.ray.origin.z + 5.0F) < 0.01F;
    const kb::render::Renderer::RuntimeSceneResourceStats stats =
        renderer.RuntimeResourceStats();
    const kb::render::SceneRenderSubmitStats submitStats =
        renderer.LastSceneSubmitStats();
    const std::optional<kb::render::ScenePostProcessSettings>&
        resolvedPostProcess = renderer.LastResolvedPostProcessSettings();
    bool layerOneCameraReceivedLight = false;
    bool layerTwoCameraReceivedLight = false;
    for (const kb::render::SceneRenderPassSubmitStats& pass :
         renderer.LastScenePassSubmitStats()) {
        if (pass.renderPass != kb::render::RenderPassKind::OpaqueScene) {
            continue;
        }
        if (pass.viewportId == 1U &&
            pass.stats.submittedForwardLightCount > 0U) {
            layerOneCameraReceivedLight = true;
        }
        if (pass.viewportId == 2U &&
            pass.stats.submittedForwardLightCount > 0U) {
            layerTwoCameraReceivedLight = true;
        }
    }
    const bool succeeded =
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::kPrimaryLocalUser) &&
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::LocalUserId{2U}) &&
        serializedFieldsLoaded &&
        scriptedMeshAssigned &&
        scriptedMaterialSlotAssigned &&
        scriptedMaterialInstanceAssigned &&
        scriptedMaterialParameterApplied &&
        serializedLightLoaded &&
        selectedPoseReachedRenderer &&
        secondaryPlayerPoseReachedRenderer &&
        stats.syncMeshSeenCount >= 1U &&
        stats.renderSceneMeshProxyCount >= 1U &&
        stats.syncLightSeenCount == 1U &&
        stats.renderSceneLightProxyCount == 1U &&
        stats.referencedMeshAssetCount >= 1U &&
        stats.referencedMaterialAssetCount >= 1U &&
        stats.cachedMeshCount >= 1U &&
        stats.cachedMaterialCount >= 1U &&
        stats.graphMaterialGpuCount == 1U &&
        stats.graphMaterialCpuFallbackCount == 0U &&
        stats.materialErrorCount == 0U;
    const bool lightRuntimeValid =
        layerOneCameraReceivedLight &&
        !layerTwoCameraReceivedLight &&
        submitStats.shadowLightEntityId == runtimeLightEntityId;
    const bool postProcessRuntimeValid =
        kb::scene::ScenePostProcessAccess::ActiveProfile(scene) != 0U &&
        resolvedPostProcess.has_value() &&
        resolvedPostProcess->bloomEnabled &&
        std::fabs(resolvedPostProcess->bloomStrength - 0.37F) < 0.001F &&
        resolvedPostProcess->fxaaEnabled &&
        !resolvedPostProcess->temporalAntiAliasingEnabled &&
        std::fabs(
            resolvedPostProcess->outputTransform.exposureStops - 0.75F) <
            0.001F;
    const std::vector<std::uint64_t> particleInstances =
        static_cast<const kb::scene::Scene&>(scene)
            .Particles()
            .LiveInstanceIds();
    const kb::assets::AssetMetadata* runtimeParticleMetadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/runtime.kbvfx");
    const bool scriptedParticleInstanceValid =
        runtimeParticleMetadata != nullptr &&
        std::any_of(
            particleInstances.begin(), particleInstances.end(),
            [&](std::uint64_t instanceId) {
                const kb::particles::ParticleRuntimeQueryResult query =
                    kb::particles::ParticlePlayback::Query(scene, instanceId);
                return query.Succeeded() && query.state &&
                    query.assetId == runtimeParticleMetadata->id.value &&
                    query.liveParticleCount == 2U;
            });
    const bool particleRuntimeValid =
        kb::particles::ParticlePlayback::HasBackend(scene) &&
        scriptedParticleInstanceValid;
    const bool scriptHasRenderFrame = scriptHost != nullptr &&
        scriptHost->SharedState()
            .Get("runtimeHasRenderFrame")
            .value_or(kb::script::ScriptValue{ false })
            .AsBool();
    const bool scriptIsVisible = scriptHost != nullptr &&
        scriptHost->SharedState()
            .Get("runtimeIsVisible")
            .value_or(kb::script::ScriptValue{ false })
            .AsBool();
    const bool scriptBoundsFound = scriptHost != nullptr &&
        scriptHost->SharedState()
            .Get("runtimeBoundsFound")
            .value_or(kb::script::ScriptValue{ false })
            .AsBool();
    const float scriptBoundsRadius = scriptHost == nullptr
        ? 0.0F
        : scriptHost->SharedState()
              .Get("runtimeBoundsRadius")
              .value_or(kb::script::ScriptValue{ 0.0F })
              .AsFloat();
    const bool scriptRenderFeedbackValid =
        scriptHasRenderFrame && scriptBoundsFound && scriptBoundsRadius > 0.0F;
    const auto sharedFloat = [scriptHost](std::string_view name) noexcept {
        return scriptHost == nullptr
            ? 0.0F
            : scriptHost->SharedState()
                  .Get(name)
                  .value_or(kb::script::ScriptValue{ 0.0F })
                  .AsFloat();
    };
    const auto sharedBool = [scriptHost](std::string_view name) noexcept {
        return scriptHost != nullptr &&
            scriptHost->SharedState()
                .Get(name)
                .value_or(kb::script::ScriptValue{ false })
                .AsBool();
    };
    const auto sharedInt = [scriptHost](std::string_view name) noexcept {
        return scriptHost == nullptr
            ? 0
            : scriptHost->SharedState()
                  .Get(name)
                  .value_or(kb::script::ScriptValue{ 0 })
                  .AsInt();
    };
    const std::string captureStatus = scriptHost == nullptr
        ? std::string{}
        : scriptHost->SharedState()
              .Get("runtimeCaptureStatus")
              .value_or(kb::script::ScriptValue{ std::string{} })
              .AsString();
    std::array<std::uint8_t, 24U> pngHeader{};
    std::ifstream capture{capturePath, std::ios::binary};
    capture.read(
        reinterpret_cast<char*>(pngHeader.data()),
        static_cast<std::streamsize>(pngHeader.size()));
    constexpr std::array<std::uint8_t, 8U> pngSignature{
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    const auto readBigEndian32 = [&pngHeader](std::size_t offset) noexcept {
        return (static_cast<std::uint32_t>(pngHeader[offset]) << 24U) |
            (static_cast<std::uint32_t>(pngHeader[offset + 1U]) << 16U) |
            (static_cast<std::uint32_t>(pngHeader[offset + 2U]) << 8U) |
            static_cast<std::uint32_t>(pngHeader[offset + 3U]);
    };
    const bool capturePngValid =
        capture.gcount() == static_cast<std::streamsize>(pngHeader.size()) &&
        std::equal(
            pngSignature.begin(), pngSignature.end(), pngHeader.begin()) &&
        readBigEndian32(16U) == kHeadlessWidth &&
        readBigEndian32(20U) == kHeadlessHeight;
    const bool scriptConversionsValid =
        sharedBool("runtimeProjectedValid") &&
        std::fabs(sharedFloat("runtimeProjectedX") - 32.0F) < 0.01F &&
        std::fabs(sharedFloat("runtimeProjectedY") - 32.0F) < 0.01F &&
        sharedBool("runtimeRayValid") &&
        std::fabs(sharedFloat("runtimeRayOriginZ") + 4.0F) < 0.01F &&
        sharedFloat("runtimeRayDirectionZ") > 0.99F &&
        sharedBool("runtimeScreenToWorldValid") &&
        std::fabs(sharedFloat("runtimeScreenToWorldZ") + 2.0F) < 0.01F;
    // bgfx's Noop renderer executes scene submission and feedback but cannot read
    // back a framebuffer. The headless CTest requires that limitation to be
    // reported explicitly, while every rendering backend must still produce PNG.
    const bool scriptCaptureValid =
        (captureStatus == "completed" && capturePngValid) ||
        (expectsUnsupportedNoopCapture && captureStatus == "failed" &&
            !capturePngValid);
    const kb::assets::AssetMetadata* activeMixerMetadata =
        scene.Assets().Manager().Registry().FindByPath(
            "/Game/runtime.kbmixer");
    const std::uint64_t scriptAudioVoice = scriptHost == nullptr
        ? 0U
        : static_cast<std::uint64_t>(
              scriptHost->SharedState()
                  .Get("runtimeAudioVoice")
                  .value_or(kb::script::ScriptValue{0})
                  .AsInt());
    const std::string scriptAudioError = scriptHost == nullptr
        ? std::string{}
        : scriptHost->SharedState()
              .Get("runtimeAudioError")
              .value_or(kb::script::ScriptValue{std::string{}})
              .AsString();
    const std::uint64_t attachedAudioVoice = scriptHost == nullptr
        ? 0U
        : static_cast<std::uint64_t>(
              scriptHost->SharedState()
                  .Get("runtimeAttachedVoice")
                  .value_or(kb::script::ScriptValue{0})
                  .AsInt());
    const bool scriptAudioVoicePlaying =
        scriptAudioVoice != 0U &&
        kb::audio::AudioPlayback::IsVoicePlaying(scene, scriptAudioVoice);
    const bool audioControlsValid =
        sharedBool("runtimeAudioPaused") &&
        sharedBool("runtimeAudioPausedState") &&
        sharedBool("runtimeAudioResumed") &&
        sharedBool("runtimeAudioSeeked") &&
        sharedBool("runtimeAudioVolumeSet") &&
        sharedBool("runtimeAudioPitchSet") &&
        sharedBool("runtimeAudioLoopSet") &&
        sharedBool("runtimeAudioStopped") &&
        sharedBool("runtimeAudioStoppedState");
    const bool attachedVoiceCleanupValid =
        attachedAudioVoice != 0U &&
        !kb::audio::AudioPlayback::IsVoicePlaying(scene, attachedAudioVoice) &&
        !kb::audio::AudioPlayback::StopVoice(scene, attachedAudioVoice);
    const std::span<const kb::scene::AudioMixerBusVolumeOverride>
        busVolumeOverrides =
            kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(scene);
    const bool audioMixerControlsValid =
        sharedBool("runtimeAudioBusSet") &&
        sharedBool("runtimeAudioBusCleared") &&
        sharedBool("runtimeAudioBusClearIdempotent") &&
        sharedBool("runtimeAudioBusFinalSet") &&
        sharedBool("runtimeAudioTransitionStarted") &&
        kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "Quiet" &&
        !kb::scene::SceneAudioMixerAccess::SnapshotTransition(scene).IsActive() &&
        busVolumeOverrides.size() == 1U &&
        busVolumeOverrides.front().bus == "Effects" &&
        std::fabs(busVolumeOverrides.front().volume - 0.55F) < 0.001F;
    const kb::scene::AudioOcclusionSettings& occlusionSettings =
        kb::scene::SceneAudioOcclusionAccess::Settings(scene);
    const kb::scene::AudioOcclusionRuntimeStats& occlusionStats =
        kb::scene::SceneAudioOcclusionAccess::RuntimeStats(scene);
    const bool audioOcclusionValid =
        sharedBool("runtimeOcclusionConfigured") &&
        sharedBool("runtimeOcclusionEnabled") &&
        occlusionSettings.enabled &&
        occlusionSettings.maxRaycastsPerTick == 1U &&
        occlusionStats.sampleRequests >= 1U &&
        occlusionStats.raycasts == 1U &&
        occlusionStats.raycasts <= occlusionSettings.maxRaycastsPerTick &&
        occlusionStats.occludedSamples >= 1U;
    const std::uint64_t markerVoice = scriptHost == nullptr
        ? 0U
        : static_cast<std::uint64_t>(
              scriptHost->SharedState()
                  .Get("runtimeAudioMarkerVoice")
                  .value_or(kb::script::ScriptValue{0})
                  .AsInt());
    const bool audioMarkerValid =
        sharedBool("runtimeAudioPositionValid") &&
        sharedFloat("runtimeAudioPositionSeconds") >= 0.0F &&
        sharedBool("runtimeAudioMarkerAdded") &&
        sharedBool("runtimeAudioMarkerReceived") &&
        markerVoice == scriptAudioVoice &&
        sharedFloat("runtimeAudioMarkerPosition") >= 0.0F;
    const bool audioRuntimeValid =
        scene.IsModuleActive("Audio.Miniaudio") &&
        kb::audio::AudioPlayback::HasBackend(scene) &&
        serializedAudioSourceLoaded &&
        serializedAudioListenerLoaded &&
        activeMixerMetadata != nullptr &&
        activeMixerMetadata->type == kb::audio::kAudioMixerAssetType &&
        kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) ==
            activeMixerMetadata->id.value &&
        kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "Quiet" &&
        scriptAudioVoicePlaying &&
        audioControlsValid &&
        attachedVoiceCleanupValid &&
        audioMixerControlsValid &&
        audioOcclusionValid &&
        audioMarkerValid;
    const bool hapticsRuntimeValid =
        sharedBool("runtimeHapticsSupported") &&
        sharedBool("runtimeHapticsDualMotor") &&
        static_cast<std::uint32_t>(
            sharedInt("runtimeHapticsMaxGamepads")) ==
            kb::input::InputDeviceState::kMaxGamepads &&
        sharedBool("runtimeHapticsLimitRejected") &&
        sharedBool("runtimeHapticsConnectionHonest") &&
        sharedBool("runtimeHapticsUserRouteValid") &&
        sharedBool("runtimeHapticsStopped");
    const bool fullySucceeded =
        succeeded && lightRuntimeValid && postProcessRuntimeValid &&
        particleRuntimeValid && scriptRenderFeedbackValid &&
        scriptConversionsValid && scriptCaptureValid && audioRuntimeValid &&
        hapticsRuntimeValid;
    std::fprintf(fullySucceeded ? stdout : stderr,
        "kb_standalone_player: camera_runtime result=%s source=project_scene "
        "camera_component=loaded clear=depth_only culling_mask=0x1 "
        "player_views=0:viewport1,2:viewport2 renderer_feedback=%s "
        "primary_pose=(%.2f,%.2f,%.2f) secondary_pose=(%.2f,%.2f,%.2f) "
        "rays_valid=(%u,%u) frames=(%u,%u) sync_cameras=%u camera_proxies=%u "
        "lua_mesh=%u lua_slot=%u lua_instance=%u lua_material_parameter=%u mesh_proxies=%u "
        "light=(loaded:%u,sync:%u,proxy:%u,layer1:%u,layer2:%u,forward:%u,shadow:%llu) "
        "post_process=(active:%u,resolved:%u,bloom:%.2f,fxaa:%u) "
        "particles=(instances:%zu,live:%u,mesh_proxies:%u,mesh_refs:%u) "
        "script_render_feedback=(valid:%u,frame:%u,visible:%u,bounds:%u,radius:%.3f) "
        "script_conversions=(valid:%u,screen:%.2f,%.2f,ray_z:%.2f,world_z:%.2f) "
        "async_capture=(status:%s,png:%u,size:%ux%u) "
        "audio=(backend:%u,source:%u,listener:%u,mixer:%u,snapshot:%u,voice:%llu,playing:%u,controls:%u,owner_cleanup:%u,mixer_controls:%u,error:%s) "
        "occlusion=(valid:%u,budget:%u,requests:%u,rays:%u,hits:%u) "
        "audio_marker=(valid:%u,voice:%llu,clock:%.3f,event:%.3f) "
        "haptics=(valid:%u,supported:%u,connected:%u,dual:%u,max:%u,limit:%u,honest:%u,stopped:%u) "
        "refs=(%u,%u) cache=(%u,%u) "
        "graph=(gpu:%u,cpu:%u) material_errors=%u\n",
        fullySucceeded ? "pass" : "fail",
        selectedPoseReachedRenderer ? "selected_camera" : "missing_or_wrong_camera",
        static_cast<double>(ray.ray.origin.x),
        static_cast<double>(ray.ray.origin.y),
        static_cast<double>(ray.ray.origin.z),
        static_cast<double>(secondaryRay.ray.origin.x),
        static_cast<double>(secondaryRay.ray.origin.y),
        static_cast<double>(secondaryRay.ray.origin.z),
        ray.valid ? 1U : 0U,
        secondaryRay.valid ? 1U : 0U,
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::kPrimaryLocalUser) ? 1U : 0U,
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::LocalUserId{2U}) ? 1U : 0U,
        stats.syncCameraSeenCount,
        stats.renderSceneCameraProxyCount,
        scriptedMeshAssigned ? 1U : 0U,
        scriptedMaterialSlotAssigned ? 1U : 0U,
        scriptedMaterialInstanceAssigned ? 1U : 0U,
        scriptedMaterialParameterApplied ? 1U : 0U,
        stats.renderSceneMeshProxyCount,
        serializedLightLoaded ? 1U : 0U,
        stats.syncLightSeenCount,
        stats.renderSceneLightProxyCount,
        layerOneCameraReceivedLight ? 1U : 0U,
        layerTwoCameraReceivedLight ? 1U : 0U,
        submitStats.submittedForwardLightCount,
        static_cast<unsigned long long>(submitStats.shadowLightEntityId),
        kb::scene::ScenePostProcessAccess::ActiveProfile(scene) != 0U ? 1U : 0U,
        resolvedPostProcess.has_value() ? 1U : 0U,
        resolvedPostProcess.has_value()
            ? static_cast<double>(resolvedPostProcess->bloomStrength)
            : 0.0,
        resolvedPostProcess.has_value() && resolvedPostProcess->fxaaEnabled
            ? 1U
            : 0U,
        particleInstances.size(),
        particleInstances.empty()
            ? 0U
            : scene.Particles().LiveParticleCount(
                  particleInstances.front()),
        stats.renderSceneMeshProxyCount,
        stats.referencedMeshAssetCount,
        scriptRenderFeedbackValid ? 1U : 0U,
        scriptHasRenderFrame ? 1U : 0U,
        scriptIsVisible ? 1U : 0U,
        scriptBoundsFound ? 1U : 0U,
        static_cast<double>(scriptBoundsRadius),
        scriptConversionsValid ? 1U : 0U,
        static_cast<double>(sharedFloat("runtimeProjectedX")),
        static_cast<double>(sharedFloat("runtimeProjectedY")),
        static_cast<double>(sharedFloat("runtimeRayDirectionZ")),
        static_cast<double>(sharedFloat("runtimeScreenToWorldZ")),
        captureStatus.c_str(),
        capturePngValid ? 1U : 0U,
        kHeadlessWidth,
        kHeadlessHeight,
        kb::audio::AudioPlayback::HasBackend(scene) ? 1U : 0U,
        serializedAudioSourceLoaded ? 1U : 0U,
        serializedAudioListenerLoaded ? 1U : 0U,
        activeMixerMetadata != nullptr &&
                kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) ==
                    activeMixerMetadata->id.value
            ? 1U
            : 0U,
        kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "Quiet"
            ? 1U
            : 0U,
        static_cast<unsigned long long>(scriptAudioVoice),
        scriptAudioVoicePlaying ? 1U : 0U,
        audioControlsValid ? 1U : 0U,
        attachedVoiceCleanupValid ? 1U : 0U,
        audioMixerControlsValid ? 1U : 0U,
        scriptAudioError.c_str(),
        audioOcclusionValid ? 1U : 0U,
        occlusionSettings.maxRaycastsPerTick,
        occlusionStats.sampleRequests,
        occlusionStats.raycasts,
        occlusionStats.occludedSamples,
        audioMarkerValid ? 1U : 0U,
        static_cast<unsigned long long>(markerVoice),
        static_cast<double>(sharedFloat("runtimeAudioPositionSeconds")),
        static_cast<double>(sharedFloat("runtimeAudioMarkerPosition")),
        hapticsRuntimeValid ? 1U : 0U,
        sharedBool("runtimeHapticsSupported") ? 1U : 0U,
        sharedBool("runtimeHapticsConnected") ? 1U : 0U,
        sharedBool("runtimeHapticsDualMotor") ? 1U : 0U,
        static_cast<unsigned int>(sharedInt("runtimeHapticsMaxGamepads")),
        sharedBool("runtimeHapticsLimitRejected") ? 1U : 0U,
        sharedBool("runtimeHapticsConnectionHonest") ? 1U : 0U,
        sharedBool("runtimeHapticsStopped") ? 1U : 0U,
        stats.referencedMeshAssetCount,
        stats.referencedMaterialAssetCount,
        stats.cachedMeshCount,
        stats.cachedMaterialCount,
        stats.graphMaterialGpuCount,
        stats.graphMaterialCpuFallbackCount,
        stats.materialErrorCount);
    std::fflush(fullySucceeded ? stdout : stderr);
    if (scriptAudioVoice != 0U) {
        static_cast<void>(
            kb::audio::AudioPlayback::StopVoice(scene, scriptAudioVoice));
    }
    return fullySucceeded;
}

[[nodiscard]] bool SubmitRuntimeFrame(kb::render::Renderer& renderer, const kb::scene::Scene& scene) {
    if (!renderer.BeginFrame()) {
        std::fprintf(stderr, "kb_standalone_player: BeginFrame failed\n");
        return false;
    }
    const bool submitted = renderer.SubmitScene(scene, HeadlessSubmitDesc());
    renderer.EndFrame();
    if (!submitted) {
        std::fprintf(stderr, "kb_standalone_player: SubmitScene failed\n");
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateAudioLifecycleRuntime(kb::scene::Scene& scene) {
    const kb::assets::AssetMetadata* clip =
        scene.Assets().Manager().Registry().FindByPath("/Game/runtime.wav");
    if (clip == nullptr || !kb::audio::AudioPlayback::HasBackend(scene)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: audio_lifecycle result=fail "
            "reason=missing_clip_or_backend\n");
        return false;
    }
    const kb::assets::AssetId clipId = clip->id;

    bool poolStarted = true;
    std::uint64_t lastVoice = 0U;
    for (std::uint32_t index = 0U; index < 12U; ++index) {
        const kb::audio::AudioPlayResult played =
            kb::audio::AudioPlayback::PlayOneShot(
                scene,
                kb::audio::AudioPlayDesc{
                    .clipAssetId = clipId.value,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                    .priority = 200U,
                });
        poolStarted = poolStarted && played.Succeeded();
        lastVoice = played.voiceId;
    }
    const bool pooledVoicePlaying =
        poolStarted && lastVoice != 0U &&
        kb::audio::AudioPlayback::IsVoicePlaying(scene, lastVoice);

    static_cast<void>(scene.Assets().Manager().Unload(clipId));
    const bool metadataSurvivedUnload =
        scene.Assets().Manager().Registry().Find(clipId) != nullptr;
    const bool voiceSurvivedUnload =
        kb::audio::AudioPlayback::IsVoicePlaying(scene, lastVoice);
    const kb::audio::AudioPlayResult afterUnload =
        kb::audio::AudioPlayback::PlayOneShot(
            scene,
            kb::audio::AudioPlayDesc{
                .clipAssetId = clipId.value,
                .volume = 0.0F,
                .loop = true,
                .spatial = false,
                .priority = 200U,
            });

    const std::vector<kb::scene::SceneEntity> roots =
        scene.Hierarchy().RootEntities();
    if (!roots.empty()) {
        kb::audio::AudioPlayback::QueueMarkerEvent(
            scene,
            kb::audio::PendingAudioMarkerEvent{
                .target = roots.front(),
                .voiceId = lastVoice,
                .marker = "outgoing-world",
                .positionSeconds = 0.0F,
            });
    }
    kb::scene::SceneDocument replacement{};
    replacement.guid = "scene:standalone-audio-lifecycle";
    replacement.name = "AudioLifecycleReplacement";
    const bool sceneChanged =
        kb::scene::SceneDocumentService::LoadIntoScene(scene, replacement);
    const bool voicesStopped =
        !kb::audio::AudioPlayback::IsVoicePlaying(scene, lastVoice) &&
        (!afterUnload.Succeeded() ||
            !kb::audio::AudioPlayback::IsVoicePlaying(
                scene, afterUnload.voiceId));
    const bool markersCleared =
        kb::audio::AudioPlayback::DrainPendingMarkerEvents(scene).empty();
    const bool backendRetained =
        kb::audio::AudioPlayback::HasBackend(scene);
    const kb::audio::AudioPlayResult afterSceneChange =
        kb::audio::AudioPlayback::PlayOneShot(
            scene,
            kb::audio::AudioPlayDesc{
                .clipAssetId = clipId.value,
                .volume = 0.0F,
                .loop = false,
                .spatial = false,
            });
    kb::audio::AudioPlayback::StopAll(scene);

    const bool succeeded =
        pooledVoicePlaying && metadataSurvivedUnload &&
        voiceSurvivedUnload && afterUnload.Succeeded() &&
        sceneChanged && voicesStopped && markersCleared &&
        backendRetained && afterSceneChange.Succeeded();
    std::fprintf(
        succeeded ? stdout : stderr,
        "kb_standalone_player: audio_lifecycle result=%s "
        "pool=%u unload=(metadata:%u,voice:%u,new:%u) "
        "scene_change=(loaded:%u,stopped:%u,markers:%u,backend:%u,new:%u)\n",
        succeeded ? "pass" : "fail",
        pooledVoicePlaying ? 1U : 0U,
        metadataSurvivedUnload ? 1U : 0U,
        voiceSurvivedUnload ? 1U : 0U,
        afterUnload.Succeeded() ? 1U : 0U,
        sceneChanged ? 1U : 0U,
        voicesStopped ? 1U : 0U,
        markersCleared ? 1U : 0U,
        backendRetained ? 1U : 0U,
        afterSceneChange.Succeeded() ? 1U : 0U);
    std::fflush(succeeded ? stdout : stderr);
    return succeeded;
}

[[nodiscard]] bool SubmitRuntimePlayerViews(
    kb::render::Renderer& renderer, const kb::scene::Scene& scene) {
    if (!renderer.BeginFrame()) {
        std::fprintf(stderr, "kb_standalone_player: BeginFrame failed\n");
        return false;
    }
    const std::array<kb::render::Renderer::SceneFrameSubmission, 2> submissions{
        kb::render::Renderer::SceneFrameSubmission{
            .scene = &scene,
            .desc = HeadlessSubmitDesc(
                1U, 0U, kb::input::kPrimaryLocalUser)},
        kb::render::Renderer::SceneFrameSubmission{
            .scene = &scene,
            .desc = HeadlessSubmitDesc(
                2U, 1U, kb::input::LocalUserId{2U})},
    };
    const bool submitted = renderer.SubmitScenes(submissions);
    renderer.EndFrame();
    if (!submitted) {
        std::fprintf(stderr,
            "kb_standalone_player: player view submission failed\n");
        return false;
    }
    return true;
}

[[nodiscard]] bool SubmitRuntimeDefaultFrame(
    kb::render::Renderer& renderer, const kb::scene::Scene& scene) {
    if (!renderer.BeginFrame()) {
        std::fprintf(stderr,
            "kb_standalone_player: default-target BeginFrame failed\n");
        return false;
    }
    renderer.SubmitScene(scene);
    renderer.EndFrame();
    return true;
}

[[nodiscard]] bool ValidateStats(const StandaloneOptions& options, const kb::render::Renderer& renderer) {
    const kb::render::Renderer::RuntimeSceneResourceStats stats = renderer.RuntimeResourceStats();
    const kb::render::MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    std::fprintf(stdout,
        "kb_standalone_player: graph_cache_root=%s graph_gpu=%u graph_cpu_fallback=%u material_errors=%u program_loads=%u program_failures=%u live_programs=%u\n",
        renderer.GraphShaderCacheRoot().c_str(),
        stats.graphMaterialGpuCount,
        stats.graphMaterialCpuFallbackCount,
        stats.materialErrorCount,
        programStats.loads,
        programStats.failures,
        programStats.liveProgramCount);
    std::fflush(stdout);

    if (options.expectedGraphGpuCount.has_value() && stats.graphMaterialGpuCount != *options.expectedGraphGpuCount) {
        std::fprintf(stderr,
            "kb_standalone_player: graph GPU count mismatch expected=%u actual=%u\n",
            *options.expectedGraphGpuCount,
            stats.graphMaterialGpuCount);
        return false;
    }
    if (stats.graphMaterialCpuFallbackCount != 0U || stats.materialErrorCount != 0U || programStats.failures != 0U) {
        std::fprintf(stderr,
            "kb_standalone_player: material graph runtime reported fallback/error cpu_fallback=%u material_errors=%u program_failures=%u\n",
            stats.graphMaterialCpuFallbackCount,
            stats.materialErrorCount,
            programStats.failures);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    StandaloneOptions options{};
    if (!ParseOptions(argc, argv, options)) {
        PrintUsage();
        return EXIT_FAILURE;
    }
    if (options.help) {
        PrintUsage();
        return EXIT_SUCCESS;
    }
    if (!options.focusProbeStopEvent.empty()) {
        kb::input::Win32InputCollector focusProbeCollector;
        StandaloneSurface focusProbeSurface;
        if (!focusProbeSurface.Initialize(true, focusProbeCollector)) {
            return EXIT_FAILURE;
        }
        static_cast<void>(BringWindowToTop(focusProbeSurface.Window()));
        static_cast<void>(SetForegroundWindow(focusProbeSurface.Window()));
        static_cast<void>(SetActiveWindow(focusProbeSurface.Window()));
        SetFocus(focusProbeSurface.Window());
        const HANDLE stopEvent = OpenEventA(
            SYNCHRONIZE, FALSE, options.focusProbeStopEvent.c_str());
        if (stopEvent == nullptr) {
            return EXIT_FAILURE;
        }
        const ULONGLONG deadline = GetTickCount64() + 10'000U;
        while (WaitForSingleObject(stopEvent, 0U) != WAIT_OBJECT_0 &&
               GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(
                       &message, nullptr, 0U, 0U, PM_REMOVE) != 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(4U);
        }
        CloseHandle(stopEvent);
        return EXIT_SUCCESS;
    }
    if (options.cameraRuntimeSelfTest &&
        (!options.projectPath.empty() ||
            !options.assetRoot.empty() ||
            options.selfTest ||
            options.inputRuntimeTest)) {
        std::fprintf(stderr,
            "kb_standalone_player: --camera-runtime-self-test cannot be combined with another runtime source\n");
        return EXIT_FAILURE;
    }
    if (!options.projectPath.empty() &&
        (!options.assetRoot.empty() ||
            options.selfTest ||
            options.inputRuntimeTest)) {
        std::fprintf(stderr,
            "kb_standalone_player: --project cannot be combined with asset or self-test runtime sources\n");
        return EXIT_FAILURE;
    }
    if (options.assetRoot.empty() &&
        options.projectPath.empty() &&
        !options.selfTest &&
        !options.cameraRuntimeSelfTest &&
        !options.inputRuntimeTest) {
        std::fprintf(stderr,
            "kb_standalone_player: provide --project, --asset-root, --self-test, "
            "--camera-runtime-self-test, or --input-runtime-test\n");
        PrintUsage();
        return EXIT_FAILURE;
    }

    std::filesystem::path selfTestPackageRoot;
    if (options.selfTest && options.assetRoot.empty() && !PrepareGraphSelfTestPackage(options, selfTestPackageRoot)) {
        return EXIT_FAILURE;
    }
    std::filesystem::path cameraRuntimePackageRoot;
    if (options.cameraRuntimeSelfTest &&
        !PrepareCameraRuntimeSelfTestProject(options, cameraRuntimePackageRoot)) {
        return EXIT_FAILURE;
    }

    std::optional<StandaloneProjectRuntimeConfig> projectRuntime;
    if (!options.projectPath.empty()) {
        projectRuntime.emplace();
        if (!ReadProjectRuntimeConfig(options, *projectRuntime)) {
            return EXIT_FAILURE;
        }
    }

    kb::input::Win32InputCollector inputCollector;
    StandaloneSurface surface;
    // Keep the automated graph cook/load check unobtrusive; normal player runs
    // and the input verification own a visible, focusable production window.
    if (!surface.Initialize(
            !options.selfTest && !options.cameraRuntimeSelfTest,
            inputCollector)) {
        std::fprintf(stderr, "kb_standalone_player: runtime window creation failed\n");
        return EXIT_FAILURE;
    }

    auto scriptModuleOwner = std::make_unique<kb::script::ScriptModule>();
    kb::script::ScriptModule* scriptModule = scriptModuleOwner.get();
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules;
    staticModules.push_back(std::move(scriptModuleOwner));
    kb::project::ProjectDescriptor sceneDescriptor =
        projectRuntime.has_value()
        ? std::move(projectRuntime->descriptor)
        : kb::project::ProjectDescriptor{};
    kb::scene::Scene scene{ std::move(sceneDescriptor), std::move(staticModules) };
    const bool scriptActive = scene.IsModuleActive("Script");
    if (scriptActive &&
        (!scriptModule->Succeeded() || scriptModule->Host() == nullptr)) {
        std::fprintf(stderr, "kb_standalone_player: script module initialization failed\n");
        for (const std::string& diagnostic : scriptModule->Diagnostics()) {
            std::fprintf(stderr, "kb_standalone_player: script module diagnostic: %s\n", diagnostic.c_str());
        }
        return EXIT_FAILURE;
    }
    if (options.inputRuntimeTest && !scriptActive) {
        std::fprintf(stderr,
            "kb_standalone_player: input runtime verification requires the Script module\n");
        return EXIT_FAILURE;
    }
    if (options.inputRuntimeTest) {
        const kb::scene::SceneObject camera =
            scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Standalone Input Runtime Camera",
            });
        scene.Components().Cameras().Set(camera.Entity(), kb::scene::CameraComponent{
            .primary = true,
            .viewportId = 1U,
        });
    } else if (projectRuntime.has_value()) {
        if (!LoadProjectScene(*projectRuntime, scene)) {
            return EXIT_FAILURE;
        }
    } else if (!options.assetRoot.empty() && !BuildSceneFromAssets(options, scene)) {
        return EXIT_FAILURE;
    }

    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = options.rendererType == bgfx::RendererType::Noop;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(options.rendererType);
    config.flushAfterRender = true;

    kb::render::Renderer renderer;
    renderer.SetGraphShaderCacheRoot(options.graphCacheRoot.generic_string());
    if (!renderer.Initialize(surface, &config)) {
        std::fprintf(stderr, "kb_standalone_player: renderer initialization failed\n");
        return EXIT_FAILURE;
    }
    kb::input::Win32XInputHapticsBackend hapticsBackend;
    kb::input::InputHaptics::RegisterBackend(scene, hapticsBackend);
    if (options.inputRuntimeTest) {
        const bool cameraPublished = SubmitRuntimeFrame(renderer, scene);
        const bool verified = cameraPublished &&
            RunStandaloneInputRuntimeVerification(
                scene, inputCollector, *scriptModule->Host(), surface.Window());
        hapticsBackend.StopAll();
        kb::input::InputHaptics::UnregisterBackend(scene, hapticsBackend);
        renderer.Shutdown();
        return verified ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Production frame order: the platform host snapshots physical devices
    // first; InputPollingSystem resolves actions during this same scene update.
    inputCollector.Collect(scene.Input().MutableDeviceState(), surface.Window());
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
    const bool submitted = options.cameraRuntimeSelfTest
        ? SubmitRuntimePlayerViews(renderer, scene)
        : SubmitRuntimeFrame(renderer, scene);
    bool cameraRuntimeFramesValid = true;
    if (options.cameraRuntimeSelfTest && submitted) {
        // The renderer publishes CPU visibility/bounds after the first submit. A normal
        // following gameplay update consumes it, runs the screen/world conversions, and
        // requests the asynchronous capture through the project Lua behaviour.
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        constexpr std::uint32_t kMaxCaptureFrames = 16U;
        for (std::uint32_t frame = 0U; frame < kMaxCaptureFrames; ++frame) {
            const std::string status = scriptModule->Host()
                ->SharedState()
                .Get("runtimeCaptureStatus")
                .value_or(kb::script::ScriptValue{std::string{}})
                .AsString();
            if (status == "completed" || status == "failed") {
                break;
            }
            if (!SubmitRuntimeDefaultFrame(renderer, scene)) {
                cameraRuntimeFramesValid = false;
                break;
            }
            static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        }
        kb::scene::SceneEntity audioOwner{};
        for (const kb::scene::SceneEntity root :
             scene.Hierarchy().RootEntities()) {
            if (scene.Entities().Name(root) == "Runtime Audio Owner") {
                audioOwner = root;
                break;
            }
        }
        if (!audioOwner.IsValid()) {
            cameraRuntimeFramesValid = false;
        } else {
            scene.Entities().Destroy(audioOwner);
            for (std::uint32_t audioTick = 0U; audioTick < 4U; ++audioTick) {
                static_cast<void>(
                    scene.Runtime().Update(1.0F / 60.0F));
            }
        }
    }
    const bool cameraRuntimeValid =
        !options.cameraRuntimeSelfTest ||
        (submitted && cameraRuntimeFramesValid &&
            ValidateProjectCameraRuntime(
                scene,
                renderer,
                scriptModule->Host(),
                cameraRuntimePackageRoot / "runtime_capture.png",
                options.rendererType == bgfx::RendererType::Noop));
    const bool audioLifecycleValid =
        !options.cameraRuntimeSelfTest ||
        (cameraRuntimeValid && ValidateAudioLifecycleRuntime(scene));
    const bool statsValid =
        submitted && cameraRuntimeValid && audioLifecycleValid &&
        ValidateStats(options, renderer);
    bool sceneReleaseValid = true;
    if (options.cameraRuntimeSelfTest && statsValid) {
        renderer.ReleaseScene(scene);
        const kb::render::Renderer::RuntimeSceneResourceStats released =
            renderer.RuntimeResourceStats();
        sceneReleaseValid =
            released.cachedMeshCount == 0U &&
            released.cachedMaterialCount == 0U &&
            released.cachedTextureCount == 0U &&
            released.renderSceneCount == 0U &&
            released.runtimeAssetDiscoverySceneCount == 0U;
        std::fprintf(
            sceneReleaseValid ? stdout : stderr,
            "kb_standalone_player: scene_release result=%s cache=(%u,%u,%u) "
            "render_scenes=%u discovery_scenes=%u\n",
            sceneReleaseValid ? "pass" : "fail",
            released.cachedMeshCount,
            released.cachedMaterialCount,
            released.cachedTextureCount,
            released.renderSceneCount,
            released.runtimeAssetDiscoverySceneCount);
        std::fflush(sceneReleaseValid ? stdout : stderr);
    }
    hapticsBackend.StopAll();
    kb::input::InputHaptics::UnregisterBackend(scene, hapticsBackend);
    renderer.Shutdown();
    const bool runtimeValid = statsValid && sceneReleaseValid;
    if (runtimeValid && !selfTestPackageRoot.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(selfTestPackageRoot, cleanupError);
    }
    if (runtimeValid && !cameraRuntimePackageRoot.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(cameraRuntimePackageRoot, cleanupError);
    }
    return runtimeValid ? EXIT_SUCCESS : EXIT_FAILURE;
}
