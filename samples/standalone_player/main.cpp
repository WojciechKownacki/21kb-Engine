#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
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
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
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
    return material;
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
    request.pass = "BaseOpaque";
    const kb::render::RenderMaterialGraphShaderBackend backend =
        kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    const kb::render::RenderMaterialGraphShaderArtifactResult cooked =
        kb::render::CookRenderMaterialGraphShaderArtifact(
            compiled.shader,
            std::span<const kb::render::RenderMaterialGraphShaderBackend>{
                &backend, 1U},
            request);
    if (!cooked.Succeeded()) {
        std::fprintf(stderr,
            "kb_standalone_player: self-test graph cook failed diagnostics=%zu\n",
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

    config.sceneReference =
        options.scenePath.empty() ? loaded.descriptor.defaultScene : options.scenePath;
    config.physicsLayersAsset = loaded.descriptor.physicsLayersAsset;
    config.inputMappingContext = loaded.descriptor.inputMappingContext;
    config.inputEnabled = loaded.descriptor.inputEnabled;
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
    WriteTriangleObj(packageRoot / "Assets" / "triangle.obj");
    if (!SaveSelfTestGraphMaterial(
            packageRoot / "Assets" / "graph.kbmat")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime material write failed\n");
        return false;
    }
    {
        std::ofstream script{
            packageRoot / "Assets" / "MeshRuntime.lua",
            std::ios::trunc};
        script
            << "function Tick(self, dt)\n"
            << "    MeshRenderer.SetMesh('/Game/triangle.obj', self.entity)\n"
            << "    MeshRenderer.SetMaterial('/Game/graph.kbmat', self.entity)\n"
            << "    MeshRenderer.SetMaterialSlot(0, '/Game/graph.kbmat', self.entity)\n"
            << "    local instance = MaterialInstance.Create('/Game/graph.kbmat')\n"
            << "    MeshRenderer.SetMaterialInstance(instance, self.entity)\n"
            << "end\n";
        if (!script) {
            std::fprintf(stderr,
                "kb_standalone_player: camera runtime Lua write failed\n");
            return false;
        }
    }

    kb::project::ProjectDescriptor descriptor{};
    descriptor.name = "CameraRuntimeSelfTest";
    descriptor.defaultScene = "/Game/Scenes/CameraRuntime.21kbscene";
    if (!kb::project::ProjectManager::SaveProject(
            packageRoot / "Project.21kbproject", descriptor)) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime project descriptor write failed\n");
        return false;
    }

    kb::scene::Scene authoringScene;
    if (!RegisterRuntimeAssetLoaders(authoringScene) ||
        !authoringScene.Assets().MountProject(packageRoot) ||
        authoringScene.Assets().Discover() != 3U) {
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
    if (meshMetadata == nullptr || materialMetadata == nullptr ||
        scriptMetadata == nullptr ||
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
    if (!kb::scene::SceneDocumentService::Save(
            authoringScene,
            packageRoot / "Assets" / "Scenes" / "CameraRuntime.21kbscene",
            "CameraRuntime")) {
        std::fprintf(stderr,
            "kb_standalone_player: camera runtime scene write failed\n");
        return false;
    }

    options.projectPath = packageRoot;
    options.rendererType = bgfx::RendererType::Noop;
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
    const kb::scene::Scene& scene,
    const kb::render::Renderer& renderer) {
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
        }
    }

    const bool secondaryPlayerPoseReachedRenderer =
        secondaryRay.valid &&
        std::fabs(secondaryRay.ray.origin.x + 8.0F) < 0.01F &&
        std::fabs(secondaryRay.ray.origin.y - 3.0F) < 0.01F &&
        std::fabs(secondaryRay.ray.origin.z + 5.0F) < 0.01F;
    const kb::render::Renderer::RuntimeSceneResourceStats stats =
        renderer.RuntimeResourceStats();
    const bool succeeded =
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::kPrimaryLocalUser) &&
        kb::scene::SceneRenderFeedback::HasFrame(
            scene, kb::input::LocalUserId{2U}) &&
        serializedFieldsLoaded &&
        scriptedMeshAssigned &&
        scriptedMaterialSlotAssigned &&
        scriptedMaterialInstanceAssigned &&
        selectedPoseReachedRenderer &&
        secondaryPlayerPoseReachedRenderer &&
        stats.syncMeshSeenCount >= 1U &&
        stats.renderSceneMeshProxyCount >= 1U &&
        stats.referencedMeshAssetCount >= 1U &&
        stats.referencedMaterialAssetCount >= 1U &&
        stats.cachedMeshCount >= 1U &&
        stats.cachedMaterialCount >= 1U &&
        stats.graphMaterialGpuCount == 1U &&
        stats.graphMaterialCpuFallbackCount == 0U &&
        stats.materialErrorCount == 0U;
    std::fprintf(succeeded ? stdout : stderr,
        "kb_standalone_player: camera_runtime result=%s source=project_scene "
        "camera_component=loaded clear=depth_only culling_mask=0x1 "
        "player_views=0:viewport1,2:viewport2 renderer_feedback=%s "
        "primary_pose=(%.2f,%.2f,%.2f) secondary_pose=(%.2f,%.2f,%.2f) "
        "rays_valid=(%u,%u) frames=(%u,%u) sync_cameras=%u camera_proxies=%u "
        "lua_mesh=%u lua_slot=%u lua_instance=%u mesh_proxies=%u "
        "refs=(%u,%u) cache=(%u,%u) "
        "graph=(gpu:%u,cpu:%u) material_errors=%u\n",
        succeeded ? "pass" : "fail",
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
        stats.renderSceneMeshProxyCount,
        stats.referencedMeshAssetCount,
        stats.referencedMaterialAssetCount,
        stats.cachedMeshCount,
        stats.cachedMaterialCount,
        stats.graphMaterialGpuCount,
        stats.graphMaterialCpuFallbackCount,
        stats.materialErrorCount);
    std::fflush(succeeded ? stdout : stderr);
    return succeeded;
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
    if (options.inputRuntimeTest) {
        const bool cameraPublished = SubmitRuntimeFrame(renderer, scene);
        const bool verified = cameraPublished &&
            RunStandaloneInputRuntimeVerification(
                scene, inputCollector, *scriptModule->Host(), surface.Window());
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
    const bool cameraRuntimeValid =
        !options.cameraRuntimeSelfTest ||
        (submitted && ValidateProjectCameraRuntime(scene, renderer));
    const bool statsValid =
        submitted && cameraRuntimeValid && ValidateStats(options, renderer);
    renderer.Shutdown();
    if (statsValid && !selfTestPackageRoot.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(selfTestPackageRoot, cleanupError);
    }
    if (statsValid && !cameraRuntimePackageRoot.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(cameraRuntimePackageRoot, cleanupError);
    }
    return statsValid ? EXIT_SUCCESS : EXIT_FAILURE;
}
