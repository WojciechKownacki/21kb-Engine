#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/TransformComponent.hpp"
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

#include <bgfx/bgfx.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <charconv>
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

class HeadlessSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return kHeadlessWidth;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return kHeadlessHeight;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

struct StandaloneOptions {
    std::filesystem::path graphCacheRoot;
    std::filesystem::path assetRoot;
    std::string mountName = "Game";
    std::string meshPath = "/Game/triangle.obj";
    std::string materialPath = "/Game/graph.kbmat";
    std::optional<std::uint32_t> expectedGraphGpuCount{};
    bgfx::RendererType::Enum rendererType = bgfx::RendererType::Noop;
    bool selfTest = false;
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
        "  --renderer=noop|d3d11\n"
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
    kb::render::RenderMaterialAssetData material = MakeSelfTestGraphMaterial();
    if (!kb::render::RenderMaterialAssetWriter::Save(packageRoot / "graph.kbmat", material)) {
        std::fprintf(stderr, "kb_standalone_player: self-test graph material write failed\n");
        return false;
    }

    const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
        material.graph,
        kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x9900U, .sourcePath = "/Game/graph.kbmat" });
    if (!compiled.Succeeded()) {
        std::fprintf(stderr, "kb_standalone_player: self-test graph compile failed diagnostics=%zu\n", compiled.diagnostics.size());
        return false;
    }

    kb::render::RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_STANDALONE_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_STANDALONE_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_STANDALONE_GRAPH_SHADER_INCLUDE_DIR, KB_STANDALONE_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = options.graphCacheRoot.generic_string();
    request.pass = "BaseOpaque";
    const kb::render::RenderMaterialGraphShaderBackend backend = kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    const kb::render::RenderMaterialGraphShaderArtifactResult cooked = kb::render::CookRenderMaterialGraphShaderArtifact(
        compiled.shader,
        std::span<const kb::render::RenderMaterialGraphShaderBackend>{ &backend, 1U },
        request);
    if (!cooked.Succeeded()) {
        std::fprintf(stderr, "kb_standalone_player: self-test graph cook failed diagnostics=%zu\n", cooked.diagnostics.size());
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : cooked.diagnostics) {
            std::fprintf(stderr, "kb_standalone_player: cook diagnostic: %s\n", diagnostic.message.c_str());
        }
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

[[nodiscard]] kb::render::SceneRenderCamera RuntimeCamera() noexcept {
    return kb::render::SceneRenderCamera{
        .view = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
        .projection = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

[[nodiscard]] kb::render::RenderSceneSubmitDesc HeadlessSubmitDesc() noexcept {
    return kb::render::RenderSceneSubmitDesc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .depthTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{1U},
                .extent = kb::render::RenderExtent{kHeadlessWidth, kHeadlessHeight},
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = RuntimeCamera(),
        .clearRgba = 0x101018FFU,
    };
}

[[nodiscard]] bool BuildSceneFromAssets(const StandaloneOptions& options, kb::scene::Scene& scene) {
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
    return true;
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
    if (options.assetRoot.empty() && !options.selfTest) {
        std::fprintf(stderr, "kb_standalone_player: provide --asset-root or --self-test\n");
        PrintUsage();
        return EXIT_FAILURE;
    }

    std::filesystem::path selfTestPackageRoot;
    if (options.selfTest && options.assetRoot.empty() && !PrepareGraphSelfTestPackage(options, selfTestPackageRoot)) {
        return EXIT_FAILURE;
    }

    kb::scene::Scene scene;
    if (!options.assetRoot.empty() && !BuildSceneFromAssets(options, scene)) {
        return EXIT_FAILURE;
    }

    HeadlessSurface surface;
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

    const bool submitted = SubmitRuntimeFrame(renderer, scene);
    const bool statsValid = submitted && ValidateStats(options, renderer);
    renderer.Shutdown();
    if (statsValid && !selfTestPackageRoot.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(selfTestPackageRoot, cleanupError);
    }
    return statsValid ? EXIT_SUCCESS : EXIT_FAILURE;
}
