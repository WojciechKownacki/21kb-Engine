#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "scene/material_preview/EditorMaterialGraphCookService.hpp"

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <filesystem>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace kb::editor::tests {
namespace {

using kb::render::RenderMaterialAssetData;
using kb::render::RenderMaterialGraphDocument;
using kb::render::RenderMaterialGraphLink;
using kb::render::RenderMaterialGraphNode;
using kb::render::RenderMaterialGraphNodeKind;

[[nodiscard]] RenderMaterialGraphLink MakeLink(
    RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    return link;
}

[[nodiscard]] RenderMaterialAssetData MakeConstantColorMaterial(std::string_view colorHint) {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = std::string{ colorHint } },
    });
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba",
        RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return material;
}

[[nodiscard]] RenderMaterialAssetData MakeQualitySwitchMaterial() {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 120,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
    });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::QualitySwitch,
        .positionX = 260,
        .positionY = 80,
    });
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba",
        RenderMaterialGraphNodeKind::QualitySwitch, 4U, "low"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba",
        RenderMaterialGraphNodeKind::QualitySwitch, 4U, "high"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::QualitySwitch, 4U, "result",
        RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return material;
}

[[nodiscard]] RenderMaterialAssetData MakeShadingPathSwitchMaterial() {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 120,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 1 0 1" },
    });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 200,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
    });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::ShadingPathSwitch,
        .positionX = 280,
        .positionY = 120,
    });
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba",
        RenderMaterialGraphNodeKind::ShadingPathSwitch, 5U, "forward"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba",
        RenderMaterialGraphNodeKind::ShadingPathSwitch, 5U, "forwardPlus"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba",
        RenderMaterialGraphNodeKind::ShadingPathSwitch, 5U, "deferred"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ShadingPathSwitch, 5U, "result",
        RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return material;
}

[[nodiscard]] RenderMaterialAssetData MakeMaterialWithoutOutput() {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    // No MaterialOutput node -> codegen fails deterministically.
    material.graph = RenderMaterialGraphDocument{};
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    return material;
}

void RunCookUnavailableTest() {
    // No shaderc configured -> service reports CookUnavailable, never crashes.
    EditorMaterialGraphCookConfig config{};
    config.cacheRoot = (std::filesystem::temp_directory_path() / "kb_cook_unavailable").generic_string();
    EditorMaterialGraphCookService service{ config };
    Require(!service.ShadercAvailable(), "KBMAT-MAT30: A service without shaderc must report the tool unavailable");

    const kb::assets::AssetId assetId{ 0x9001U };
    const EditorMaterialGraphCookResult result = service.CookNow(assetId, MakeConstantColorMaterial("1 1 1 1"));
    Require(result.status == EditorMaterialGraphCookStatus::CookUnavailable,
        "KBMAT-MAT30: Cooking without shaderc must yield CookUnavailable, not a silent fallback");
    Require(!result.diagnostics.empty(),
        "KBMAT-MAT30: CookUnavailable must surface a diagnostic explaining the missing tool");
    Require(!result.HasGpuProgram(),
        "KBMAT-MAT30: CookUnavailable must not claim a GPU program");

    // Async request through an unavailable service publishes the unavailable state without hanging.
    service.RequestCook(assetId, MakeConstantColorMaterial("1 1 1 1"));
    service.WaitForIdle();
    Require(service.LatestResult(assetId).status == EditorMaterialGraphCookStatus::CookUnavailable,
        "KBMAT-MAT30: An unavailable cook service must still expose a visible state, not Idle/empty");
}

void RunFailedGraphTest() {
    EditorMaterialGraphCookConfig config{};
    config.cacheRoot = (std::filesystem::temp_directory_path() / "kb_cook_failed").generic_string();
#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
    config.shadercPath = KB_EDITOR_GRAPH_SHADERC_PATH;
    config.varyingDefPath = KB_EDITOR_GRAPH_SHADER_VARYING_DEF;
    config.includeDirs = { KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR, KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR };
#endif
    config.backend = kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    EditorMaterialGraphCookService service{ config };

    const kb::assets::AssetId assetId{ 0x9002U };
    const EditorMaterialGraphCookResult result = service.CookNow(assetId, MakeMaterialWithoutOutput());
#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
    Require(result.status == EditorMaterialGraphCookStatus::Failed,
        "KBMAT-MAT30: A graph that fails codegen must report Failed");
    Require(!result.diagnostics.empty(),
        "KBMAT-MAT30: A failed cook must carry codegen diagnostics with node/pin context");
    Require(!result.HasGpuProgram(),
        "KBMAT-MAT30: A failed cook must not claim a GPU program");
#else
    Require(result.status == EditorMaterialGraphCookStatus::CookUnavailable,
        "KBMAT-MAT30: Without shaderc the failed-graph path still reports a visible unavailable state");
#endif
}

void RunEditorOnlyGraphChangesKeepCookVariantIdentityTest() {
    EditorMaterialGraphCookConfig config{};
    config.cacheRoot = (std::filesystem::temp_directory_path() / "kb_cook_semantic_identity").generic_string();
    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x9026U };

    RenderMaterialAssetData original = MakeConstantColorMaterial("1 0 0 1");
    RenderMaterialAssetData organized = original;
    organized.graph.nodes[1].positionX = 4096;
    organized.graph.nodes[1].positionY = -2048;
    organized.graph.nodes[1].parameter.displayName = "Editor-only rename";
    organized.graph.comments.push_back(kb::render::RenderMaterialGraphCommentBox{
        .id = 8U, .positionX = 0, .positionY = 0, .width = 640, .height = 480, .text = "Layout",
    });
    const EditorMaterialGraphCookResult first = service.CookNow(assetId, original);
    const EditorMaterialGraphCookResult editorOnly = service.CookNow(assetId, organized);
    Require(first.variantKey == editorOnly.variantKey,
        "P2.6: layout, comment and display-name edits must preserve the canonical cook variant identity");

    RenderMaterialAssetData semantic = original;
    semantic.graph.nodes[1].parameter.defaultValueHint = "0 1 0 1";
    const EditorMaterialGraphCookResult changed = service.CookNow(assetId, semantic);
    Require(!(first.variantKey == changed.variantKey),
        "P2.6: a shader-semantic graph edit must produce a different cook variant identity");
}

#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
[[nodiscard]] EditorMaterialGraphCookConfig MakeCookConfig(std::string cacheSubdir) {
    EditorMaterialGraphCookConfig config{};
    config.shadercPath = KB_EDITOR_GRAPH_SHADERC_PATH;
    config.varyingDefPath = KB_EDITOR_GRAPH_SHADER_VARYING_DEF;
    config.includeDirs = { KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR, KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    config.cacheRoot = (std::filesystem::path{ KB_EDITOR_GRAPH_SHADER_CACHE_DIR } / cacheSubdir).generic_string();
    config.backend = kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    config.debounceMs = 60U;
    return config;
}

void RunSynchronousCookProducesBinaryTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("mat30_sync");
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    Require(service.ShadercAvailable(), "KBMAT-MAT30: Test toolchain must expose shaderc");

    const kb::assets::AssetId assetId{ 0x3001U };
    const EditorMaterialGraphCookResult first = service.CookNow(assetId, MakeConstantColorMaterial("0.2 0.4 0.6 1"));
    Require(first.status == EditorMaterialGraphCookStatus::Ready,
        "KBMAT-MAT30: Cooking a valid graph must produce ready GPU binaries");
    Require(first.graphSourceHash != 0U, "KBMAT-MAT30: A cooked graph must expose its source hash");
    Require(first.passes.size() == 4U, "Deferred default cook must cover BaseOpaque, GBuffer, ShadowDepth and BaseTransparent");
    Require(first.compiledPassCount == first.passes.size() && first.cacheHitPassCount == 0U && first.cacheEntryCount > 0U,
        "KBMAT-MAT69: A fresh cook must report compiled passes and cache footprint telemetry");
    for (const EditorMaterialGraphCookPassResult& pass : first.passes) {
        Require(pass.succeeded, "KBMAT-MAT30: Every cooked pass must succeed");
        Require(!pass.cacheHit, "KBMAT-MAT30: A fresh cook must be a real compile, not a cache hit");
        Require(std::filesystem::exists(pass.binaryPath, error) && std::filesystem::file_size(pass.binaryPath, error) > 0U,
            "KBMAT-MAT30: Each cooked pass must leave a non-empty binary in the cache");
        Require(pass.binaryByteSize > 0U, "KBMAT-MAT69: Cook pass telemetry must report binary byte size");
    }
    bool sawGBuffer = false;
    for (const EditorMaterialGraphCookPassResult& pass : first.passes) {
        if (pass.pass == "GBuffer") {
            sawGBuffer = pass.binaryPath.find("/GBuffer/") != std::string::npos || pass.binaryPath.find("\\GBuffer\\") != std::string::npos;
        }
    }
    Require(sawGBuffer, "Deferred graph cook must produce a distinct GBuffer pass artifact path");

    // Re-cooking the unchanged graph must dedupe on the source/cook hash (no recompile).
    const EditorMaterialGraphCookResult second = service.CookNow(assetId, MakeConstantColorMaterial("0.2 0.4 0.6 1"));
    Require(second.status == EditorMaterialGraphCookStatus::UpToDate,
        "KBMAT-MAT30: Re-cooking an unchanged graph must report UpToDate via the cache");
    Require(second.cacheHitPassCount == second.passes.size() && second.compiledPassCount == 0U,
        "KBMAT-MAT69: Re-cooking an unchanged graph must report pass-level cache hits");
    for (const EditorMaterialGraphCookPassResult& pass : second.passes) {
        Require(pass.succeeded && pass.cacheHit,
            "KBMAT-MAT30: Re-cooking an unchanged graph must hit the cache for every pass");
    }

    // A distinct graph must produce a distinct hash and a distinct binary.
    const EditorMaterialGraphCookResult other = service.CookNow(kb::assets::AssetId{ 0x3002U }, MakeConstantColorMaterial("0.9 0.1 0.3 1"));
    Require(other.status == EditorMaterialGraphCookStatus::Ready,
        "KBMAT-MAT30: A distinct graph must cook its own program");
    Require(other.graphSourceHash != first.graphSourceHash,
        "KBMAT-MAT30: Distinct graphs must have distinct source hashes");
    Require(other.passes.front().binaryPath != first.passes.front().binaryPath,
        "KBMAT-MAT30: Distinct graphs must stage distinct binaries");
}

void RunCookBudgetTelemetryWarningTest() {
    EditorMaterialGraphCookConfig config = MakeCookConfig("mat69_budget");
    config.cacheEntryWarningThreshold = 0U;
    config.cacheByteWarningThreshold = 0U;
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x6900U };
    const EditorMaterialGraphCookResult first = service.CookNow(assetId, MakeConstantColorMaterial("0.3 0.6 0.9 1"));
    Require(first.status == EditorMaterialGraphCookStatus::Ready &&
            first.compiledPassCount == first.passes.size() &&
            first.cacheHitPassCount == 0U &&
            first.cacheEntryCount > 0U &&
            first.cacheByteSize > 0U,
        "KBMAT-MAT69: Cook telemetry must expose compile count and cache footprint");
    bool foundBudgetWarning = first.budgetWarning;
    for (const std::string& diagnostic : first.diagnostics) {
        foundBudgetWarning = foundBudgetWarning || diagnostic.find("graph cook budget exceeded") != std::string::npos;
    }
    Require(foundBudgetWarning,
        "KBMAT-MAT69: Exceeding cook/cache budgets must emit a visible warning diagnostic");

    const EditorMaterialGraphCookResult second = service.CookNow(assetId, MakeConstantColorMaterial("0.3 0.6 0.9 1"));
    Require(second.status == EditorMaterialGraphCookStatus::UpToDate &&
            second.cacheHitPassCount == second.passes.size() &&
            second.compiledPassCount == 0U,
        "KBMAT-MAT69: Budget telemetry must preserve cache-hit reporting on unchanged cooks");
}

void RunQualityVariantCookContextTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("mat52_quality_preview");
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x5208U };
    const RenderMaterialAssetData material = MakeQualitySwitchMaterial();

    const EditorMaterialGraphCookResult low = service.CookNow(
        assetId,
        material,
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = assetId.value,
            .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::Low,
        });
    const EditorMaterialGraphCookResult high = service.CookNow(
        assetId,
        material,
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = assetId.value,
            .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High,
        });

    Require(low.HasGpuProgram() && high.HasGpuProgram(),
        "KBMAT-MAT52: Quality preview variants must both cook real GPU binaries");
    Require(low.graphSourceHash != 0U && high.graphSourceHash != 0U && low.graphSourceHash != high.graphSourceHash,
        "KBMAT-MAT52: Cook service must include preview quality in the graph shader hash");
    Require(!low.passes.empty() && !high.passes.empty() && low.passes.front().binaryPath != high.passes.front().binaryPath,
        "KBMAT-MAT52: Quality preview variants must stage distinct shader binaries");
}

void RunShadingPathVariantCookContextTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("mat52_shading_path_preview");
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x5218U };
    const RenderMaterialAssetData material = MakeShadingPathSwitchMaterial();

    const auto cookPath = [&service, assetId, &material](kb::render::RenderMaterialGraphShadingPath path) {
        return service.CookNow(
            assetId,
            material,
            kb::render::RenderMaterialGraphBuildContext{
                .assetId = assetId.value,
                .shadingPath = path,
            });
    };

    const EditorMaterialGraphCookResult forward = cookPath(kb::render::RenderMaterialGraphShadingPath::Forward);
    const EditorMaterialGraphCookResult forwardPlus = cookPath(kb::render::RenderMaterialGraphShadingPath::ForwardPlus);
    const EditorMaterialGraphCookResult deferred = cookPath(kb::render::RenderMaterialGraphShadingPath::Deferred);

    Require(forward.HasGpuProgram() && forwardPlus.HasGpuProgram() && deferred.HasGpuProgram(),
        "KBMAT-MAT52: Forward, Forward+ and Deferred shading-path preview variants must cook real GPU binaries");
    Require(forward.graphSourceHash != 0U && forwardPlus.graphSourceHash != 0U && deferred.graphSourceHash != 0U,
        "KBMAT-MAT52: Shading-path preview variants must expose non-zero graph shader hashes");
    Require(forward.graphSourceHash != forwardPlus.graphSourceHash &&
            forward.graphSourceHash != deferred.graphSourceHash &&
            forwardPlus.graphSourceHash != deferred.graphSourceHash,
        "KBMAT-MAT52: Cook service must include Forward/Forward+/Deferred shading path in the graph shader hash");
    Require(!forward.passes.empty() && !forwardPlus.passes.empty() && !deferred.passes.empty(),
        "KBMAT-MAT52: Shading-path preview variants must produce pass cook telemetry");
    Require(forward.passes.front().binaryPath != forwardPlus.passes.front().binaryPath &&
            forward.passes.front().binaryPath != deferred.passes.front().binaryPath &&
            forwardPlus.passes.front().binaryPath != deferred.passes.front().binaryPath,
        "KBMAT-MAT52: Forward, Forward+ and Deferred shading-path preview variants must stage distinct shader binaries");
}

void RunAsyncDebouncedCookTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("mat30_async");
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x3100U };

    // Two rapid edits inside the debounce window must collapse into a single cook of the latest graph.
    service.RequestCook(assetId, MakeConstantColorMaterial("0.1 0.2 0.3 1"));
    const std::uint64_t latestGeneration = service.RequestCook(assetId, MakeConstantColorMaterial("0.7 0.8 0.9 1"));
    service.WaitForIdle();

    const std::vector<EditorMaterialGraphCookResult> drained = service.DrainResults();
    Require(drained.size() == 1U, "KBMAT-MAT30: Debounce must collapse rapid edits into a single completion");
    Require(drained.front().requestGeneration == latestGeneration,
        "KBMAT-MAT30: The surviving cook must be the most recent edit, not a superseded one");
    Require(drained.front().status == EditorMaterialGraphCookStatus::Ready,
        "KBMAT-MAT30: The async cook must produce ready GPU binaries");

    const EditorMaterialGraphCookResult latest = service.LatestResult(assetId);
    Require(latest.status == EditorMaterialGraphCookStatus::Ready && latest.HasGpuProgram(),
        "KBMAT-MAT30: LatestResult must reflect the completed GPU cook");
    for (const EditorMaterialGraphCookPassResult& pass : latest.passes) {
        Require(std::filesystem::exists(pass.binaryPath, error),
            "KBMAT-MAT30: The async cook must leave its binaries on disk");
    }

    // Draining again returns nothing new.
    Require(service.DrainResults().empty(), "KBMAT-MAT30: Completions must only be drained once");
}

void RunAsyncVariantCoexistenceTest() {
    const RenderMaterialAssetData material = MakeQualitySwitchMaterial();
    const kb::assets::AssetId assetId{ 0x3110U };
    const std::array<kb::render::RenderMaterialGraphBuildContext, 3U> contexts{
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = assetId.value,
            .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::Low,
            .variantUsage = kb::render::RenderMaterialGraphVariantUsage::Preview,
        },
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = assetId.value,
            .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High,
            .shadingPath = kb::render::RenderMaterialGraphShadingPath::Deferred,
            .variantUsage = kb::render::RenderMaterialGraphVariantUsage::Scene,
        },
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = assetId.value,
            .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::Medium,
            .variantUsage = kb::render::RenderMaterialGraphVariantUsage::NodePreview,
        },
    };

    const auto runOrder = [&](std::string_view suffix, bool reverse) {
        EditorMaterialGraphCookConfig config = MakeCookConfig("p11_variant_coexistence_" + std::string{ suffix });
        config.debounceMs = 10U;
        std::error_code error;
        std::filesystem::remove_all(config.cacheRoot, error);
        EditorMaterialGraphCookService service{ config };
        for (std::size_t offset = 0U; offset < contexts.size(); ++offset) {
            const std::size_t index = reverse ? contexts.size() - 1U - offset : offset;
            service.RequestCook(assetId, material, contexts[index]);
        }
        service.WaitForIdle();
        const std::vector<EditorMaterialGraphCookResult> results = service.DrainResults();
        Require(results.size() == contexts.size(),
            "P1.1: preview, scene and node-preview requests for one material must coexist in the debounce queue");

        std::array<bool, 3U> seen{};
        for (const EditorMaterialGraphCookResult& result : results) {
            Require(result.HasGpuProgram() && result.variantKey.materialAssetId == assetId.value,
                "P1.1: every independently queued variant must produce a real GPU artifact");
            const std::size_t slot = result.variantKey.usage == kb::render::RenderMaterialGraphVariantUsage::Preview
                ? 0U
                : result.variantKey.usage == kb::render::RenderMaterialGraphVariantUsage::Scene ? 1U : 2U;
            seen[slot] = true;
            const EditorMaterialGraphCookResult latest = service.LatestResult(result.variantKey);
            Require(latest.variantKey.SameProgramFamily(result.variantKey) && latest.HasGpuProgram(),
                "P1.1: status lookup must be scoped to the exact cook variant family");
        }
        Require(seen[0] && seen[1] && seen[2],
            "P1.1: cook completion reporting lost a preview, scene or node-preview variant");
    };

    runOrder("forward", false);
    runOrder("reverse", true);
}

void RunHotReloadLastGoodTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("mat33_lastgood");
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);

    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x3300U };

    // 1) A valid edit cooks a GPU program and becomes last-good.
    service.RequestCook(assetId, MakeConstantColorMaterial("0.2 0.5 0.8 1"));
    service.WaitForIdle();
    const EditorMaterialGraphCookResult ready = service.LatestResult(assetId);
    Require(ready.status == EditorMaterialGraphCookStatus::Ready, "KBMAT-MAT33: A valid edit must cook to Ready");
    const std::uint64_t goodHash = ready.graphSourceHash;
    static_cast<void>(service.DrainResults());

    // 2) A broken edit must keep the last-good program live and report Stale (not Failed/black).
    service.RequestCook(assetId, MakeMaterialWithoutOutput());
    service.WaitForIdle();
    const EditorMaterialGraphCookResult stale = service.LatestResult(assetId);
    Require(stale.status == EditorMaterialGraphCookStatus::Stale,
        "KBMAT-MAT33: A broken edit with a previous good cook must report Stale, not drop the program");
    Require(stale.HasGpuProgram() && stale.graphSourceHash == goodHash,
        "KBMAT-MAT33: Stale must keep the last-good GPU program/hash live");
    Require(!stale.diagnostics.empty(),
        "KBMAT-MAT33: Stale must still surface the failing edit's diagnostics");

    // 3) Fixing the graph hot-reloads back to a Ready GPU program without a restart.
    service.RequestCook(assetId, MakeConstantColorMaterial("0.9 0.3 0.1 1"));
    service.WaitForIdle();
    const EditorMaterialGraphCookResult fixed = service.LatestResult(assetId);
    Require(fixed.status == EditorMaterialGraphCookStatus::Ready || fixed.status == EditorMaterialGraphCookStatus::UpToDate,
        "KBMAT-MAT33: Fixing the graph must hot-reload back to a GPU-ready program");
    Require(fixed.HasGpuProgram() && fixed.graphSourceHash != goodHash,
        "KBMAT-MAT33: The fixed graph must bind a new program, not the stale one");
}
#endif

void RunCookBannerMappingTest() {
    // MAT-32: every cook status maps to a visible banner; Ready/UpToDate read as GPU ready,
    // failures as error, missing tool as a fallback warning, and Idle shows nothing.
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Ready).severity == EditorMaterialGraphCookBannerSeverity::Ready,
        "KBMAT-MAT32: Ready cook must map to the GPU-ready banner severity");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::UpToDate).severity == EditorMaterialGraphCookBannerSeverity::Ready,
        "KBMAT-MAT32: UpToDate cook must also read as GPU ready");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Cooking).severity == EditorMaterialGraphCookBannerSeverity::Pending,
        "KBMAT-MAT32: An in-flight cook must show a compiling banner");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Failed).severity == EditorMaterialGraphCookBannerSeverity::Error,
        "KBMAT-MAT32: A failed cook must show an error-material banner");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::CookUnavailable).severity == EditorMaterialGraphCookBannerSeverity::Warning,
        "KBMAT-MAT32: An unavailable cook must show a fallback warning banner");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Stale).severity == EditorMaterialGraphCookBannerSeverity::Warning,
        "KBMAT-MAT33: A stale (last-good) cook must show a warning banner, not an error");
    Require(MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Idle).severity == EditorMaterialGraphCookBannerSeverity::None,
        "KBMAT-MAT32: An idle material must not show a banner");
    Require(!MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus::Ready).label.empty(),
        "KBMAT-MAT32: A visible banner must carry a label");
}

} // namespace

void RunEditorMaterialGraphCookServiceTests() {
    RunCookUnavailableTest();
    RunFailedGraphTest();
    RunEditorOnlyGraphChangesKeepCookVariantIdentityTest();
    RunCookBannerMappingTest();
#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
    RunSynchronousCookProducesBinaryTest();
    RunCookBudgetTelemetryWarningTest();
    RunQualityVariantCookContextTest();
    RunShadingPathVariantCookContextTest();
    RunAsyncDebouncedCookTest();
    RunAsyncVariantCoexistenceTest();
    RunHotReloadLastGoodTest();
#endif
}

} // namespace kb::editor::tests
