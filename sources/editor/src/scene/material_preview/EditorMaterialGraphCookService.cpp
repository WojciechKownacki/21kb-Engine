#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "localization/EditorLocalization.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderPrewarmer.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderToolchain.hpp"
#include "kb/render/resources/RenderMaterialSemanticHash.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::string SeverityTag(kb::render::RenderMaterialGraphDiagnosticSeverity severity) {
    return severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error ? "error" : "warning";
}

[[nodiscard]] std::string FormatDiagnostic(const kb::render::RenderMaterialGraphDiagnostic& diagnostic) {
    std::string text = "[" + SeverityTag(diagnostic.severity) + "]";
    if (diagnostic.nodeId != 0U) {
        text += " node=" + std::to_string(diagnostic.nodeId);
    }
    if (!diagnostic.pin.empty()) {
        text += " pin=" + diagnostic.pin;
    }
    if (!diagnostic.pass.empty()) {
        text += " pass=" + diagnostic.pass;
    }
    if (!diagnostic.backend.empty()) {
        text += " backend=" + diagnostic.backend;
    }
    text += ": " + diagnostic.message;
    return text;
}

[[nodiscard]] std::vector<std::string> EffectivePasses(const EditorMaterialGraphCookConfig& config) {
    if (!config.passes.empty()) {
        return config.passes;
    }
    // BaseOpaque (GPU forward), GBuffer (deferred MRT), ShadowDepth (graph alpha clip),
    // BaseTransparent (active alpha-blended submit pass) and MotionVectors (graphs that move, offset the
    // depth of or clip their surface) all have a live runtime consumer.
    return { kb::render::kRenderMaterialGraphScenePasses.begin(), kb::render::kRenderMaterialGraphScenePasses.end() };
}

void HashVariantBytes(std::uint64_t& hash, std::string_view value) noexcept {
    for (const char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    hash ^= 0xffU;
    hash *= 1099511628211ULL;
}

[[nodiscard]] std::string CanonicalPassSet(const EditorMaterialGraphCookConfig& config) {
    std::string result;
    for (const std::string& pass : EffectivePasses(config)) {
        if (!result.empty()) {
            result.push_back(';');
        }
        result += pass;
    }
    return result;
}

[[nodiscard]] bool IsDynamicParameterNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::ParameterScalar ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterVector ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor;
}

// The identity of the shader code a material produces. A scalar, vector or color parameter is a uniform:
// its default is a value handed to the program, not a part of it, so changing it must not look like a new
// shader that needs compiling.
[[nodiscard]] std::uint64_t ShaderCodeHash(const kb::render::RenderMaterialAssetData& material) {
    const bool hasDefaults = std::ranges::any_of(material.graph.nodes, [](const kb::render::RenderMaterialGraphNode& node) {
        return IsDynamicParameterNode(node.kind) && !node.parameter.defaultValueHint.empty();
    });
    if (!hasDefaults) {
        return kb::render::RenderMaterialShaderSemanticHash(material);
    }
    kb::render::RenderMaterialAssetData code{};
    code.materialType = material.materialType;
    code.materialTypeVersion = material.materialTypeVersion;
    code.graph = material.graph;
    for (kb::render::RenderMaterialGraphNode& node : code.graph.nodes) {
        if (IsDynamicParameterNode(node.kind)) {
            node.parameter.defaultValueHint.clear();
        }
    }
    return kb::render::RenderMaterialShaderSemanticHash(code);
}

[[nodiscard]] kb::render::RenderMaterialGraphVariantKey BuildCookVariantKey(
    const EditorMaterialGraphCookConfig& config,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialGraphBuildContext& context) {
    std::uint64_t inputHash = ShaderCodeHash(material);
    HashVariantBytes(inputHash, config.shadercPath);
    HashVariantBytes(inputHash, config.varyingDefPath);
    for (const std::string& includeDir : config.includeDirs) {
        HashVariantBytes(inputHash, includeDir);
    }
    for (const std::string& dependencyFile : config.dependencyFiles) {
        HashVariantBytes(inputHash, dependencyFile);
    }
    const std::string passSet = CanonicalPassSet(config);
    HashVariantBytes(inputHash, passSet);
    HashVariantBytes(inputHash, std::to_string(config.materialTypeVersion));
    return kb::render::RenderMaterialGraphVariantKey{
        .materialAssetId = assetId.value,
        .usage = context.variantUsage,
        .qualityLevel = static_cast<std::uint8_t>(context.qualityLevel),
        .featureLevel = static_cast<std::uint8_t>(context.featureLevel),
        .shadingPath = static_cast<std::uint8_t>(context.shadingPath),
        .shaderStage = static_cast<std::uint8_t>(context.shaderStage),
        .pass = passSet,
        .backend = static_cast<std::uint32_t>(config.backend),
        .binaryInputHash = inputHash == 0U ? 1U : inputHash,
    };
}

template <typename Map>
void EraseSupersededProgramFamily(Map& map, const kb::render::RenderMaterialGraphVariantKey& key) {
    for (auto it = map.begin(); it != map.end();) {
        if (it->first.SameProgramFamily(key) && !(it->first == key)) {
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

struct CookCacheFootprint {
    std::uint32_t entryCount = 0U;
    std::uint64_t byteSize = 0U;
};

[[nodiscard]] CookCacheFootprint MeasureCookCacheFootprint(std::string_view cacheRoot) {
    CookCacheFootprint footprint{};
    if (cacheRoot.empty()) {
        return footprint;
    }
    std::error_code error;
    const std::filesystem::path root{ std::string{ cacheRoot } };
    if (!std::filesystem::exists(root, error)) {
        return footprint;
    }
    for (std::filesystem::recursive_directory_iterator it{ root, error }, end; it != end && !error; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            continue;
        }
        if (footprint.entryCount < std::numeric_limits<std::uint32_t>::max()) {
            ++footprint.entryCount;
        }
        const std::uintmax_t fileSize = it->file_size(error);
        if (!error) {
            footprint.byteSize += fileSize;
        }
        error.clear();
    }
    return footprint;
}

void AppendCookNote(EditorMaterialGraphCookResult& result, std::string text) {
    result.diagnostics.push_back(std::move(text));
    result.diagnosticNodeIds.push_back(0U);
}

// `markNode` false keeps the line clickable (it still names its node) without putting a second mark on the node.
void AppendCookDiagnostic(
    EditorMaterialGraphCookResult& result,
    const kb::render::RenderMaterialGraphDiagnostic& diagnostic,
    bool markNode = true) {
    result.diagnostics.push_back(FormatDiagnostic(diagnostic));
    result.diagnosticNodeIds.push_back(diagnostic.nodeId);
    if (markNode && diagnostic.nodeId != 0U) {
        result.nodeDiagnostics.push_back(diagnostic);
    }
}

// The shader compiler rejected the code of one node: say so on that node, in words that need no knowledge of shaders.
void AppendCookNodeRejected(EditorMaterialGraphCookResult& result, std::uint32_t nodeId, const std::string& pass) {
    AppendCookDiagnostic(result, kb::render::RenderMaterialGraphDiagnostic{
        .severity = kb::render::RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = kb::render::RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .nodeId = nodeId,
        .pass = pass,
        .message = "This node could not be turned into a shader. Check its values, its connections and any code it contains.",
    });
}

void AppendCookBudgetWarnings(const EditorMaterialGraphCookConfig& config, EditorMaterialGraphCookResult& result) {
    const auto warn = [&result](std::string message) {
        result.budgetWarning = true;
        AppendCookNote(result, "[warning]: graph cook budget exceeded - " + std::move(message));
    };
    if (result.elapsedMs > config.compileWarningMs) {
        warn("cook time " + std::to_string(result.elapsedMs) + "ms/" + std::to_string(config.compileWarningMs) + "ms");
    }
    if (result.cacheEntryCount > config.cacheEntryWarningThreshold) {
        warn("cache entries " + std::to_string(result.cacheEntryCount) + "/" + std::to_string(config.cacheEntryWarningThreshold));
    }
    if (result.cacheByteSize > config.cacheByteWarningThreshold) {
        warn("cache bytes " + std::to_string(result.cacheByteSize) + "/" + std::to_string(config.cacheByteWarningThreshold));
    }
}

// A graph turned into shader source, with the passes that still have to be compiled. Everything up to the
// compiler runs in here; it is fast and runs once per request. `shader` is null when there is nothing to
// compile (no compiler, or the graph does not generate code) and `result` is then already final.
struct CookPlan {
    EditorMaterialGraphCookResult result;
    std::shared_ptr<const kb::render::RenderMaterialGraphShaderSource> shader;
    std::vector<std::string> passes;
    std::uint32_t materialTypeVersion = 1U;
    std::chrono::steady_clock::time_point startedAt{};
};

// What compiling one pass produced.
struct PassCookOutcome {
    EditorMaterialGraphCookPassResult pass;
    std::vector<kb::render::RenderMaterialGraphDiagnostic> diagnostics;
};

[[nodiscard]] CookPlan PlanCook(
    const EditorMaterialGraphCookConfig& config,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) {
    CookPlan plan{};
    plan.startedAt = std::chrono::steady_clock::now();
    plan.materialTypeVersion = material.materialTypeVersion;
    EditorMaterialGraphCookResult& result = plan.result;
    result.materialAssetId = assetId;
    result.variantKey = BuildCookVariantKey(config, assetId, material, graphContext);
    result.materialTypeVersion = material.materialTypeVersion;

    if (config.shadercPath.empty()) {
        result.status = EditorMaterialGraphCookStatus::CookUnavailable;
        AppendCookNote(result, "[error]: graph shader cook unavailable - shaderc tool not found (set KB_GRAPH_SHADERC or build bgfx::shaderc)");
        return plan;
    }

    graphContext.assetId = assetId.value;
    kb::render::RenderMaterialGraphCompileResult compiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(material.graph, graphContext);
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
        AppendCookDiagnostic(result, diagnostic);
    }
    if (!compiled.Succeeded()) {
        result.status = EditorMaterialGraphCookStatus::Failed;
        return plan;
    }
    result.graphSourceHash = compiled.shader.sourceHash;
    result.backendName = std::string{ kb::render::RenderMaterialGraphShaderBackendName(config.backend) };
    result.textureBindingCount = static_cast<std::uint32_t>(compiled.shader.reflection.textures.size()) +
        (compiled.shader.reflection.usesSceneColor ? 1U : 0U) +
        (compiled.shader.reflection.usesSceneDepth ? 1U : 0U);
    result.uniformCount = static_cast<std::uint32_t>(compiled.shader.reflection.uniforms.size());
    result.varyingCount = static_cast<std::uint32_t>(compiled.shader.reflection.requiredVaryings.size());

    for (const std::string& pass : EffectivePasses(config)) {
        if (kb::render::RenderMaterialGraphCooksScenePass(compiled.shader.reflection, pass)) {
            plan.passes.push_back(pass);
        }
    }
    plan.shader = std::make_shared<const kb::render::RenderMaterialGraphShaderSource>(std::move(compiled.shader));
    return plan;
}

// Compiles one pass of a planned graph through the shared graph shader cache. Safe to run for several
// passes at the same time: every pass owns its cache directory and the cache locks an entry while it
// is written.
[[nodiscard]] PassCookOutcome CompilePass(
    const EditorMaterialGraphCookConfig& config,
    const kb::render::RenderMaterialGraphShaderSource& shader,
    std::uint32_t materialTypeVersion,
    const std::string& pass,
    const std::atomic<bool>* terminate) {
    PassCookOutcome outcome{};
    outcome.pass.pass = pass;

    kb::render::RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = config.shadercPath;
    request.varyingDefPath = config.varyingDefPath;
    request.includeDirs = config.includeDirs;
    request.dependencyFiles = config.dependencyFiles;
    request.cacheRoot = config.cacheRoot;
    request.pass = pass;
    request.shaderPlatform = config.shaderPlatform;
    request.materialTypeVersion = materialTypeVersion;
    request.terminate = terminate;

    const std::array<kb::render::RenderMaterialGraphShaderBackend, 1U> backends{ config.backend };
    const kb::render::RenderMaterialGraphShaderArtifactResult cook =
        kb::render::CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    outcome.diagnostics = cook.diagnostics;

    const kb::render::RenderMaterialGraphShaderBinary* binary =
        cook.Succeeded() && cook.artifact.has_value() ? cook.artifact->FindBinary(config.backend) : nullptr;
    if (binary == nullptr) {
        return outcome;
    }
    outcome.pass.succeeded = true;
    outcome.pass.cacheHit = binary->cacheHit;
    outcome.pass.binaryPath = binary->binaryPath;
    outcome.pass.binaryByteSize = binary->byteSize;
    // Off-thread shader warm: creating the freshly cooked DXBC on the live D3D11 device here forces the
    // driver's DXBC->ISA compile to happen *now* (invisibly, off the render thread) instead of inside the
    // next bgfx::frame() when the preview first draws with it -- which otherwise froze the whole editor UI
    // for ~2s per new graph shader. No-op on non-D3D11 backends; deduped by bytecode so repeated cooks of
    // the same shader stay cheap.
    if (!binary->binaryPath.empty()) {
        static_cast<void>(kb::render::PrewarmGraphShaderFile(binary->binaryPath));
    }
    return outcome;
}

// Puts the passes of a plan together, in the order the plan lists them, whatever order they finished in.
[[nodiscard]] EditorMaterialGraphCookResult FinishCook(
    const EditorMaterialGraphCookConfig& config,
    CookPlan& plan,
    std::vector<PassCookOutcome>& outcomes) {
    EditorMaterialGraphCookResult result = std::move(plan.result);
    if (plan.shader != nullptr) {
        bool anyFailure = false;
        bool allCacheHit = true;
        std::vector<std::uint32_t> rejectedNodes;
        for (PassCookOutcome& outcome : outcomes) {
            if (!outcome.pass.succeeded) {
                // One short sentence a person can act on, ahead of the compiler's own words: on the node the
                // compiler's message points at, or for the whole material when no node can be told.
                const auto named = std::ranges::find_if(outcome.diagnostics, [](const kb::render::RenderMaterialGraphDiagnostic& diagnostic) {
                    return diagnostic.nodeId != 0U;
                });
                if (named != outcome.diagnostics.end()) {
                    if (std::ranges::find(rejectedNodes, named->nodeId) == rejectedNodes.end()) {
                        rejectedNodes.push_back(named->nodeId);
                        AppendCookNodeRejected(result, named->nodeId, outcome.pass.pass);
                    }
                } else if (!anyFailure) {
                    AppendCookNote(result, "[error]: This material could not be turned into a shader. Check the node you changed last.");
                }
            }
            for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : outcome.diagnostics) {
                AppendCookDiagnostic(result, diagnostic, false);
            }
            if (outcome.pass.succeeded) {
                if (outcome.pass.cacheHit) {
                    ++result.cacheHitPassCount;
                } else {
                    ++result.compiledPassCount;
                }
                allCacheHit = allCacheHit && outcome.pass.cacheHit;
            } else {
                anyFailure = true;
                allCacheHit = false;
            }
            result.passes.push_back(std::move(outcome.pass));
        }
        result.status = anyFailure
            ? EditorMaterialGraphCookStatus::Failed
            : (allCacheHit ? EditorMaterialGraphCookStatus::UpToDate : EditorMaterialGraphCookStatus::Ready);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - plan.startedAt).count();
    result.elapsedMs = elapsed <= 0
        ? 0U
        : static_cast<std::uint32_t>(std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max()));
    CookCacheFootprint footprint = MeasureCookCacheFootprint(config.cacheRoot);
    if (footprint.byteSize > config.cacheByteLimit) {
        // Down to three quarters of the limit, so the next cook does not have to prune again.
        static_cast<void>(kb::render::PruneRenderMaterialGraphShaderCache(config.cacheRoot,
            kb::render::RenderMaterialGraphShaderCachePolicy{
                .maxBytes = config.cacheByteLimit,
                .targetBytes = config.cacheByteLimit / 4U * 3U }));
        footprint = MeasureCookCacheFootprint(config.cacheRoot);
    }
    result.cacheEntryCount = footprint.entryCount;
    result.cacheByteSize = footprint.byteSize;
    AppendCookBudgetWarnings(config, result);
    return result;
}

[[nodiscard]] std::uint32_t ResolveCompileParallelism(const EditorMaterialGraphCookConfig& config) noexcept {
    if (config.maxParallelCompiles != 0U) {
        return config.maxParallelCompiles;
    }
    constexpr std::uint32_t kMostAutomaticCompiles = 6U;
    const std::uint32_t hardware = std::thread::hardware_concurrency();
    return std::clamp<std::uint32_t>(hardware > 1U ? hardware - 1U : 1U, 1U, kMostAutomaticCompiles);
}

} // namespace

std::string_view EditorMaterialGraphCookStatusName(EditorMaterialGraphCookStatus status) noexcept {
    switch (status) {
    case EditorMaterialGraphCookStatus::Idle:
        return "Idle";
    case EditorMaterialGraphCookStatus::Pending:
        return "Pending";
    case EditorMaterialGraphCookStatus::Cooking:
        return "Cooking";
    case EditorMaterialGraphCookStatus::Ready:
        return "Ready";
    case EditorMaterialGraphCookStatus::UpToDate:
        return "UpToDate";
    case EditorMaterialGraphCookStatus::Stale:
        return "Stale";
    case EditorMaterialGraphCookStatus::Failed:
        return "Failed";
    case EditorMaterialGraphCookStatus::CookUnavailable:
        return "CookUnavailable";
    }
    return "Idle";
}

EditorMaterialGraphCookBanner MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus status) {
    switch (status) {
    case EditorMaterialGraphCookStatus::Pending:
    case EditorMaterialGraphCookStatus::Cooking:
        return { std::string{ EditorText("materialEditor.cook.compiling", "Compiling graph shader...") }, EditorMaterialGraphCookBannerSeverity::Pending };
    case EditorMaterialGraphCookStatus::Ready:
    case EditorMaterialGraphCookStatus::UpToDate:
        return { std::string{ EditorText("materialEditor.cook.ready", "Ready (GPU graph)") }, EditorMaterialGraphCookBannerSeverity::Ready };
    case EditorMaterialGraphCookStatus::Stale:
        return { std::string{ EditorText("materialEditor.cook.stale", "Stale (last-good) - fix graph to refresh") }, EditorMaterialGraphCookBannerSeverity::Warning };
    case EditorMaterialGraphCookStatus::Failed:
        return { std::string{ EditorText("materialEditor.cook.failed", "Failed (error material) - see diagnostics") }, EditorMaterialGraphCookBannerSeverity::Error };
    case EditorMaterialGraphCookStatus::CookUnavailable:
        return { std::string{ EditorText("materialEditor.cook.fallback", "Fallback (CPU/builtin) - shaderc unavailable") }, EditorMaterialGraphCookBannerSeverity::Warning };
    case EditorMaterialGraphCookStatus::Idle:
        break;
    }
    return { {}, EditorMaterialGraphCookBannerSeverity::None };
}

kb::render::RenderMaterialGraphShaderBackend EditorMaterialGraphCookConfig::BackendForActiveRenderer() noexcept {
    return kb::render::RenderMaterialGraphShaderBackendForActiveRenderer();
}

EditorMaterialGraphCookConfig EditorMaterialGraphCookConfig::Resolve(std::string cacheRoot) {
    EditorMaterialGraphCookConfig config{};
    config.cacheRoot = std::move(cacheRoot);
    config.backend = BackendForActiveRenderer();

    // The paths this build baked in; the shared resolution adds the KB_GRAPH_SHADERC override and
    // the discovery next to the running editor.
    kb::render::RenderMaterialGraphShaderToolchain builtIn{};
#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
    builtIn.shadercPath = KB_EDITOR_GRAPH_SHADERC_PATH;
#endif
#if defined(KB_EDITOR_GRAPH_SHADER_VARYING_DEF)
    builtIn.varyingDefPath = KB_EDITOR_GRAPH_SHADER_VARYING_DEF;
#endif
#if defined(KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR)
    builtIn.includeDirs.emplace_back(KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR);
#endif
#if defined(KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR)
    builtIn.includeDirs.emplace_back(KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR);
#endif
    const kb::render::RenderMaterialGraphShaderToolchain toolchain =
        kb::render::ResolveRenderMaterialGraphShaderToolchain(builtIn);
    config.shaderPlatform = toolchain.shaderPlatform;
    config.shadercPath = toolchain.shadercPath;
    config.varyingDefPath = toolchain.varyingDefPath;
    config.includeDirs = toolchain.includeDirs;
    return config;
}

EditorMaterialGraphCookService::EditorMaterialGraphCookService()
    : EditorMaterialGraphCookService(EditorMaterialGraphCookConfig{}) {
}

// One cook in flight: the plan, one outcome slot per pass, and what the passes need to agree on.
struct EditorMaterialGraphCookService::CookJob {
    kb::render::RenderMaterialGraphVariantKey key{};
    std::uint64_t generation = 0U;
    EditorMaterialGraphCookConfig config;
    CookPlan plan;
    std::vector<PassCookOutcome> outcomes;
    std::atomic<std::size_t> remaining{ 0U };
    // Set by a newer request for the same variant: passes that have not started are skipped.
    std::atomic<bool> cancelled{ false };
    // Set once the newer request is compiling itself: the compilers this job has running are ended.
    std::atomic<bool> terminate{ false };
    // Passes the job skipped because it was cancelled; a job that skipped one has no complete result to go back to.
    std::atomic<std::uint32_t> skippedPasses{ 0U };
    // Passes compiling right now.
    std::atomic<std::uint32_t> activePasses{ 0U };
    // Set for CookNow(), which waits for the last pass instead of publishing.
    std::promise<void> completion;

    // Compiles pass `index` unless the job was cancelled. True when this was the job's last pass.
    bool RunPass(
        std::size_t index,
        std::atomic<std::uint32_t>& running,
        std::atomic<std::uint32_t>& peak,
        std::atomic<std::uint32_t>& skipped) {
        if (cancelled.load(std::memory_order_acquire)) {
            skipped.fetch_add(1U, std::memory_order_relaxed);
            skippedPasses.fetch_add(1U, std::memory_order_relaxed);
        } else {
            activePasses.fetch_add(1U, std::memory_order_acq_rel);
            const std::uint32_t now = running.fetch_add(1U, std::memory_order_acq_rel) + 1U;
            std::uint32_t seen = peak.load(std::memory_order_relaxed);
            while (now > seen && !peak.compare_exchange_weak(seen, now, std::memory_order_relaxed)) {
            }
            try {
                outcomes[index] = CompilePass(config, *plan.shader, plan.materialTypeVersion, plan.passes[index], &terminate);
            } catch (const std::exception& error) {
                outcomes[index] = PassCookOutcome{};
                outcomes[index].pass.pass = plan.passes[index];
                outcomes[index].diagnostics.push_back(kb::render::RenderMaterialGraphDiagnostic{
                    .severity = kb::render::RenderMaterialGraphDiagnosticSeverity::Error,
                    .pass = plan.passes[index],
                    .message = std::string{ "The shader compiler stopped unexpectedly: " } + error.what(),
                });
            }
            running.fetch_sub(1U, std::memory_order_acq_rel);
            activePasses.fetch_sub(1U, std::memory_order_acq_rel);
        }
        return remaining.fetch_sub(1U, std::memory_order_acq_rel) == 1U;
    }
};

EditorMaterialGraphCookService::EditorMaterialGraphCookService(EditorMaterialGraphCookConfig config)
    : config_(std::move(config)),
      lane_{ std::make_unique<kb::assets::streaming::BackgroundLane>(
          kb::assets::streaming::BackgroundLoadService::Shared(), kb::assets::streaming::BackgroundJobClass::Long) },
      compileLane_{ std::make_unique<kb::assets::streaming::BackgroundLane>(
          kb::assets::streaming::BackgroundLoadService::Shared(), kb::assets::streaming::BackgroundJobClass::Long,
          ResolveCompileParallelism(config_)) } {
}

EditorMaterialGraphCookService::~EditorMaterialGraphCookService() {
    StopWorker();
}

bool EditorMaterialGraphCookService::ShadercAvailable() const noexcept {
    return !config_.shadercPath.empty();
}

std::uint64_t EditorMaterialGraphCookService::RequestCook(
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) {
    EditorMaterialGraphCookConfig snapshot;
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        // Track the live renderer backend so cooked binaries land in the directory the runtime
        // program loader reads from for the active bgfx renderer.
        config_.backend = EditorMaterialGraphCookConfig::BackendForActiveRenderer();
        snapshot = config_;
    }
    // Hashing the graph is the costly part of a request; it runs before the lock is taken so a worker that
    // publishes a result is never held up by it.
    const kb::render::RenderMaterialGraphVariantKey variantKey =
        BuildCookVariantKey(snapshot, assetId, material, graphContext);

    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        // A newer source revision supersedes only the same program family. Preview, scene and node-preview
        // variants remain independently queued and reported. Revisions that are still queued are dropped,
        // and revisions already compiling stop starting new passes: their results are never published.
        EraseSupersededProgramFamily(pending_, variantKey);
        for (const std::shared_ptr<CookJob>& job : inFlightJobs_) {
            if (job->key.SameProgramFamily(variantKey) && !(job->key == variantKey)) {
                job->cancelled.store(true, std::memory_order_release);
            }
        }
        EraseSupersededProgramFamily(latest_, variantKey);
        // Back to the edit that is still compiling (undo before the newer edit started): its compile is
        // cheaper than starting over, so it goes on and the compilers of the edit it replaced are ended.
        for (const std::shared_ptr<CookJob>& job : inFlightJobs_) {
            if (job->key == variantKey && job->cancelled.load(std::memory_order_acquire) &&
                !job->terminate.load(std::memory_order_acquire) && job->skippedPasses.load(std::memory_order_acquire) == 0U) {
                job->cancelled.store(false, std::memory_order_release);
                TerminateSupersededJobsLocked(*job);
                EditorMaterialGraphCookResult cooking{};
                cooking.materialAssetId = assetId;
                cooking.variantKey = variantKey;
                cooking.materialTypeVersion = material.materialTypeVersion;
                cooking.requestGeneration = job->generation;
                cooking.status = EditorMaterialGraphCookStatus::Cooking;
                latest_[variantKey] = std::move(cooking);
                return job->generation;
            }
        }
        // The same shader code again (a parameter value changed, a click that moved nothing) is already
        // queued, compiling or done: there is nothing to compile.
        if (const auto same = latest_.find(variantKey); same != latest_.end()) {
            const EditorMaterialGraphCookStatus status = same->second.status;
            if (status == EditorMaterialGraphCookStatus::Pending || status == EditorMaterialGraphCookStatus::Cooking ||
                status == EditorMaterialGraphCookStatus::Ready || status == EditorMaterialGraphCookStatus::UpToDate) {
                return same->second.requestGeneration;
            }
        }
        generation = ++generationCounter_;
        // A variant that was cooked before has its binaries in the cache: nothing is being typed that the
        // wait could coalesce, so it goes straight to the cache check.
        const bool cookedBefore =
            std::find(knownVariants_.begin(), knownVariants_.end(), variantKey) != knownVariants_.end();
        PendingEntry entry{};
        entry.material = material;
        entry.graphContext = std::move(graphContext);
        entry.generation = generation;
        entry.readyAt = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(cookedBefore ? 0U : snapshot.debounceMs);
        pending_[variantKey] = std::move(entry);

        EditorMaterialGraphCookResult pendingResult{};
        pendingResult.materialAssetId = assetId;
        pendingResult.variantKey = variantKey;
        pendingResult.materialTypeVersion = material.materialTypeVersion;
        pendingResult.requestGeneration = generation;
        pendingResult.status = snapshot.shadercPath.empty()
            ? EditorMaterialGraphCookStatus::CookUnavailable
            : EditorMaterialGraphCookStatus::Pending;
        latest_[variantKey] = std::move(pendingResult);
        ScheduleDrainLocked();
    }
    wakeCv_.notify_all();
    return generation;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::CookNow(
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) const {
    EditorMaterialGraphCookConfig snapshot;
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        snapshot = config_;
    }
    const auto job = std::make_shared<CookJob>();
    job->plan = PlanCook(snapshot, assetId, material, std::move(graphContext));
    job->config = std::move(snapshot);
    const std::size_t passCount = job->plan.shader != nullptr ? job->plan.passes.size() : 0U;
    job->outcomes.resize(passCount);
    job->remaining.store(passCount, std::memory_order_release);
    std::future<void> done = job->completion.get_future();
    for (std::size_t index = 0U; index < passCount; ++index) {
        static_cast<void>(compileLane_->Run([this, job, index](std::string&) {
            if (job->RunPass(index, runningCompiles_, peakCompiles_, skippedCompiles_)) {
                job->completion.set_value();
            }
            return true;
        }));
    }
    if (passCount != 0U) {
        done.wait();
    }
    return FinishCook(job->config, job->plan, job->outcomes);
}

std::vector<EditorMaterialGraphCookResult> EditorMaterialGraphCookService::DrainResults() {
    std::lock_guard<std::mutex> lock{ mutex_ };
    std::vector<EditorMaterialGraphCookResult> drained;
    drained.swap(completed_);
    return drained;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::LatestResult(kb::assets::AssetId assetId) const {
    std::lock_guard<std::mutex> lock{ mutex_ };
    const EditorMaterialGraphCookResult* newest = nullptr;
    for (const auto& [key, result] : latest_) {
        if (key.materialAssetId == assetId.value &&
            (newest == nullptr || result.requestGeneration > newest->requestGeneration)) {
            newest = &result;
        }
    }
    if (newest != nullptr) {
        return *newest;
    }
    EditorMaterialGraphCookResult idle{};
    idle.materialAssetId = assetId;
    idle.status = EditorMaterialGraphCookStatus::Idle;
    return idle;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::LatestResult(
    const kb::render::RenderMaterialGraphVariantKey& variantKey) const {
    std::lock_guard<std::mutex> lock{ mutex_ };
    const auto exact = latest_.find(variantKey);
    if (exact != latest_.end()) {
        return exact->second;
    }
    for (const auto& [key, result] : latest_) {
        if (key.SameProgramFamily(variantKey)) {
            return result;
        }
    }
    EditorMaterialGraphCookResult idle{};
    idle.materialAssetId = kb::assets::AssetId{ variantKey.materialAssetId };
    idle.variantKey = variantKey;
    return idle;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::LatestResult(
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialGraphBuildContext& graphContext) const {
    kb::render::RenderMaterialGraphVariantKey family{};
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        family.materialAssetId = assetId.value;
        family.usage = graphContext.variantUsage;
        family.qualityLevel = static_cast<std::uint8_t>(graphContext.qualityLevel);
        family.featureLevel = static_cast<std::uint8_t>(graphContext.featureLevel);
        family.shadingPath = static_cast<std::uint8_t>(graphContext.shadingPath);
        family.shaderStage = static_cast<std::uint8_t>(graphContext.shaderStage);
        family.pass = CanonicalPassSet(config_);
        family.backend = static_cast<std::uint32_t>(config_.backend);
        for (const auto& [key, result] : latest_) {
            if (key.SameProgramFamily(family)) {
                return result;
            }
        }
    }
    EditorMaterialGraphCookResult idle{};
    idle.materialAssetId = assetId;
    idle.variantKey = std::move(family);
    return idle;
}

void EditorMaterialGraphCookService::WaitForIdle() {
    std::unique_lock<std::mutex> lock{ mutex_ };
    idleCv_.wait(lock, [this] { return pending_.empty() && inFlight_ == 0U; });
}

void EditorMaterialGraphCookService::ScheduleDrainLocked() {
    if (drainScheduled_ || stop_) {
        return;
    }
    // The lane runs one drain at a time and never needs this mutex to queue it.
    static_cast<void>(lane_->Run([this](std::string&) {
        DrainPending();
        return true;
    }));
    drainScheduled_ = true;
}

void EditorMaterialGraphCookService::StopWorker() {
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        stop_ = true;
        pending_.clear();
        for (const std::shared_ptr<CookJob>& job : inFlightJobs_) {
            job->cancelled.store(true, std::memory_order_release);
        }
    }
    wakeCv_.notify_all();
    // Drops a drain that has not started and waits for a running one, which sees stop_; then the same for
    // the compiles, which skip their passes once cancelled.
    lane_.reset();
    compileLane_.reset();
}

std::uint32_t EditorMaterialGraphCookService::PeakParallelCompiles() const noexcept {
    return peakCompiles_.load(std::memory_order_relaxed);
}

std::uint32_t EditorMaterialGraphCookService::SkippedCompileCount() const noexcept {
    return skippedCompiles_.load(std::memory_order_relaxed);
}

std::uint32_t EditorMaterialGraphCookService::TerminatedCompileCount() const noexcept {
    return terminatedCompiles_.load(std::memory_order_relaxed);
}

void EditorMaterialGraphCookService::TerminateSupersededJobsLocked(const CookJob& current) {
    for (const std::shared_ptr<CookJob>& job : inFlightJobs_) {
        if (job.get() == &current || !job->key.SameProgramFamily(current.key) ||
            !job->cancelled.load(std::memory_order_acquire) || job->terminate.exchange(true, std::memory_order_acq_rel)) {
            continue;
        }
        terminatedCompiles_.fetch_add(job->activePasses.load(std::memory_order_acquire), std::memory_order_relaxed);
    }
}

bool EditorMaterialGraphCookService::SupersededLocked(
    const kb::render::RenderMaterialGraphVariantKey& key, std::uint64_t generation) const {
    for (const auto& [pendingKey, pendingEntry] : pending_) {
        if (pendingKey.SameProgramFamily(key) && pendingEntry.generation > generation) {
            return true;
        }
    }
    return false;
}

void EditorMaterialGraphCookService::PublishResultLocked(EditorMaterialGraphCookResult result) {
    // Hot-reload last-good policy (MAT-33): a successful cook becomes the new last-good; a
    // failed/unavailable cook keeps the previous good program live and reports Stale so the
    // viewport never drops to a black/error frame while the artist is mid-edit.
    if (result.status == EditorMaterialGraphCookStatus::Ready || result.status == EditorMaterialGraphCookStatus::UpToDate) {
        EraseSupersededProgramFamily(lastGood_, result.variantKey);
        lastGood_[result.variantKey] = result;
        constexpr std::size_t kKnownVariantLimit = 64U;
        if (std::find(knownVariants_.begin(), knownVariants_.end(), result.variantKey) == knownVariants_.end()) {
            knownVariants_.push_back(result.variantKey);
            if (knownVariants_.size() > kKnownVariantLimit) {
                knownVariants_.pop_front();
            }
        }
    } else if (result.status == EditorMaterialGraphCookStatus::Failed || result.status == EditorMaterialGraphCookStatus::CookUnavailable) {
        const EditorMaterialGraphCookResult* good = nullptr;
        for (const auto& [goodKey, goodResult] : lastGood_) {
            if (goodKey.SameProgramFamily(result.variantKey)) {
                good = &goodResult;
                break;
            }
        }
        if (good != nullptr) {
            EditorMaterialGraphCookResult stale = *good;
            stale.status = EditorMaterialGraphCookStatus::Stale;
            stale.requestGeneration = result.requestGeneration;
            stale.diagnostics = result.diagnostics; // carry the failure reason for the panel
            stale.diagnosticNodeIds = result.diagnosticNodeIds;
            stale.nodeDiagnostics = result.nodeDiagnostics;
            result = std::move(stale);
        }
    }
    EraseSupersededProgramFamily(latest_, result.variantKey);
    latest_[result.variantKey] = result;
    completed_.push_back(std::move(result));
}

void EditorMaterialGraphCookService::RunPassJob(const std::shared_ptr<CookJob>& job, std::size_t index) {
    if (job->RunPass(index, runningCompiles_, peakCompiles_, skippedCompiles_)) {
        PublishJob(job);
    }
}

void EditorMaterialGraphCookService::PublishJob(const std::shared_ptr<CookJob>& job) {
    // A job a newer edit cancelled has nothing to report; skip putting its result together.
    EditorMaterialGraphCookResult result;
    if (!job->cancelled.load(std::memory_order_acquire)) {
        result = FinishCook(job->config, job->plan, job->outcomes);
        result.requestGeneration = job->generation;
    }
    std::lock_guard<std::mutex> lock{ mutex_ };
    inFlightJobs_.erase(std::remove(inFlightJobs_.begin(), inFlightJobs_.end(), job), inFlightJobs_.end());
    if (!stop_ && !job->cancelled.load(std::memory_order_acquire) && !SupersededLocked(job->key, job->generation)) {
        PublishResultLocked(std::move(result));
    }
    --inFlight_;
    idleCv_.notify_all();
}

void EditorMaterialGraphCookService::DrainPending() {
    std::unique_lock<std::mutex> lock{ mutex_ };
    while (true) {
        if (stop_ || pending_.empty()) {
            drainScheduled_ = false;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto dueIt = pending_.end();
        auto earliest = std::chrono::steady_clock::time_point::max();
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->second.readyAt <= now) {
                dueIt = it;
                break;
            }
            earliest = std::min(earliest, it->second.readyAt);
        }
        if (dueIt == pending_.end()) {
            wakeCv_.wait_until(lock, earliest);
            continue;
        }

        const kb::render::RenderMaterialGraphVariantKey variantKey = dueIt->first;
        PendingEntry entry = std::move(dueIt->second);
        pending_.erase(dueIt);
        ++inFlight_;

        const kb::assets::AssetId assetId{ variantKey.materialAssetId };
        if (const auto latestIt = latest_.find(variantKey); latestIt != latest_.end()) {
            latestIt->second.status = EditorMaterialGraphCookStatus::Cooking;
        }
        const auto job = std::make_shared<CookJob>();
        job->key = variantKey;
        job->generation = entry.generation;
        job->config = config_;

        // The graph becomes shader source here, on the drain worker, and only the compiles are handed on.
        lock.unlock();
        job->plan = PlanCook(job->config, assetId, entry.material, std::move(entry.graphContext));
        lock.lock();

        if (stop_ || SupersededLocked(variantKey, entry.generation)) {
            --inFlight_;
            idleCv_.notify_all();
            continue;
        }
        if (job->plan.shader == nullptr || job->plan.passes.empty()) {
            // Nothing to compile (no compiler, a graph that does not generate code): the plan is the result.
            lock.unlock();
            EditorMaterialGraphCookResult result = FinishCook(job->config, job->plan, job->outcomes);
            result.requestGeneration = entry.generation;
            lock.lock();
            if (!stop_ && !SupersededLocked(variantKey, entry.generation)) {
                PublishResultLocked(std::move(result));
            }
            --inFlight_;
            idleCv_.notify_all();
            continue;
        }

        const std::size_t passCount = job->plan.passes.size();
        job->outcomes.resize(passCount);
        job->remaining.store(passCount, std::memory_order_release);
        inFlightJobs_.push_back(job);
        // This edit is compiling now, so the older ones it replaced have nothing left to wait for.
        TerminateSupersededJobsLocked(*job);
        lock.unlock();
        for (std::size_t index = 0U; index < passCount; ++index) {
            static_cast<void>(compileLane_->Run([this, job, index](std::string&) {
                RunPassJob(job, index);
                return true;
            }));
        }
        lock.lock();
    }
}

} // namespace kb::editor
