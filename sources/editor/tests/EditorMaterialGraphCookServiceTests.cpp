#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "scene/material/MaterialEditorState.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
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

[[nodiscard]] RenderMaterialAssetData MakeParameterArithmeticMaterial(
    RenderMaterialGraphNodeKind kind, std::string_view outputPin, std::string_view hint) {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U, .kind = kind, .positionX = 40, .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "tuned", .displayName = "Tuned", .defaultValueHint = std::string{ hint } } });
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U, .kind = RenderMaterialGraphNodeKind::Multiply, .positionX = 200, .positionY = 40 });
    material.graph.links.push_back(MakeLink(kind, 2U, std::string{ outputPin }, RenderMaterialGraphNodeKind::Multiply, 3U, "a"));
    material.graph.links.push_back(MakeLink(kind, 2U, std::string{ outputPin }, RenderMaterialGraphNodeKind::Multiply, 3U, "b"));
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::Multiply, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return material;
}

// A scalar wired into a color slot: the graph is refused and the refusal names the output node whose
// slot the scalar does not fit.
[[nodiscard]] RenderMaterialAssetData MakeScalarIntoColorMaterial() {
    RenderMaterialAssetData material{};
    material.materialType = "graph";
    material.materialTypeVersion = 1U;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .positionX = 40, .positionY = 40 });
    material.graph.links.push_back(MakeLink(
        RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return material;
}

[[nodiscard]] std::string ReadBinaryFile(const std::string& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void RunParallelPassCompilesAreDeterministicTest() {
    // The same graph compiled one pass at a time and with every pass at once must give the same result,
    // in the same pass order, with byte-identical binaries.
    const RenderMaterialAssetData material = MakeConstantColorMaterial("0.31 0.62 0.93 1");
    const kb::assets::AssetId assetId{ 0x7A01U };
    std::error_code error;

    EditorMaterialGraphCookConfig serialConfig = MakeCookConfig("p45_parallel_serial");
    serialConfig.maxParallelCompiles = 1U;
    std::filesystem::remove_all(serialConfig.cacheRoot, error);
    EditorMaterialGraphCookService serialService{ serialConfig };
    const EditorMaterialGraphCookResult serial = serialService.CookNow(assetId, material);

    EditorMaterialGraphCookConfig parallelConfig = MakeCookConfig("p45_parallel_wide");
    parallelConfig.maxParallelCompiles = 4U;
    std::filesystem::remove_all(parallelConfig.cacheRoot, error);
    EditorMaterialGraphCookService parallelService{ parallelConfig };
    const EditorMaterialGraphCookResult parallel = parallelService.CookNow(assetId, material);

    Require(serial.status == EditorMaterialGraphCookStatus::Ready && parallel.status == EditorMaterialGraphCookStatus::Ready,
        "P4.5: serial and parallel cooks of a valid graph must both compile it");
    Require(serial.passes.size() == 4U && serial.passes.size() == parallel.passes.size(),
        "P4.5: both cooks must cover the same passes");
    Require(serial.compiledPassCount == 4U && parallel.compiledPassCount == 4U,
        "P4.5: a cold cook must really compile every pass");
    Require(serialService.PeakParallelCompiles() == 1U,
        "P4.5: a service limited to one compile must never run two at once");
    Require(parallelService.PeakParallelCompiles() >= 2U,
        "P4.5: the passes of one cold cook must be compiled at the same time when the limit allows it");
    Require(parallelService.PeakParallelCompiles() <= 4U,
        "P4.5: parallel compiles must stay within the configured limit");
    for (std::size_t index = 0U; index < serial.passes.size(); ++index) {
        Require(serial.passes[index].pass == parallel.passes[index].pass,
            "P4.5: pass results must keep the plan's order whatever order the compiles finished in");
        Require(serial.passes[index].succeeded && parallel.passes[index].succeeded &&
                serial.passes[index].binaryByteSize == parallel.passes[index].binaryByteSize,
            "P4.5: parallel compiles must produce the binaries serial ones do");
        Require(ReadBinaryFile(serial.passes[index].binaryPath) == ReadBinaryFile(parallel.passes[index].binaryPath),
            "P4.5: a parallel compile must be byte-for-byte what a serial one is");
    }
    Require(serial.graphSourceHash == parallel.graphSourceHash && serial.diagnostics == parallel.diagnostics,
        "P4.5: parallelism must not change the source or the diagnostics of a cook");
}

void RunStaleCompilesAreCancelledTest() {
    // One compile at a time: while the first pass of graph A compiles, graph B replaces it. A's other
    // passes must never start and A's result must never be published.
    EditorMaterialGraphCookConfig config = MakeCookConfig("p45_cancel");
    config.maxParallelCompiles = 1U;
    config.debounceMs = 10U;
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);
    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x7A02U };

    const std::uint64_t first = service.RequestCook(assetId, MakeConstantColorMaterial("0.1 0.2 0.3 1"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (service.PeakParallelCompiles() == 0U && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(service.PeakParallelCompiles() == 1U, "P4.5: the first graph must have started compiling");
    const std::uint64_t second = service.RequestCook(assetId, MakeConstantColorMaterial("0.9 0.8 0.7 1"));
    Require(second > first, "P4.5: the replacing request must be the newer one");
    service.WaitForIdle();

    Require(service.SkippedCompileCount() >= 1U,
        "P4.5: passes of a replaced graph that had not started must be skipped, not compiled");
    const std::vector<EditorMaterialGraphCookResult> results = service.DrainResults();
    Require(results.size() == 1U && results.front().requestGeneration == second,
        "P4.5: only the replacing graph may report a result; the replaced one must stay silent");
    Require(results.front().status == EditorMaterialGraphCookStatus::Ready && results.front().passes.size() == 4U,
        "P4.5: the replacing graph must be compiled completely");
    Require(service.LatestResult(assetId).requestGeneration == second,
        "P4.5: the latest result must belong to the replacing graph");
}

void RunReopenAndUndoHitTheCacheTest() {
    EditorMaterialGraphCookConfig config = MakeCookConfig("p45_reopen");
    config.debounceMs = 600U;
    std::error_code error;
    std::filesystem::remove_all(config.cacheRoot, error);
    const kb::assets::AssetId assetId{ 0x7A03U };
    const RenderMaterialAssetData first = MakeConstantColorMaterial("0.2 0.3 0.4 1");
    const RenderMaterialAssetData second = MakeConstantColorMaterial("0.6 0.5 0.4 1");
    {
        EditorMaterialGraphCookService service{ config };
        service.RequestCook(assetId, first);
        service.WaitForIdle();
        service.RequestCook(assetId, second);
        service.WaitForIdle();
        static_cast<void>(service.DrainResults());

        // Undo: the first graph is back. It was cooked before, so nothing waits for more edits and the
        // cache serves every pass.
        const auto startedAt = std::chrono::steady_clock::now();
        service.RequestCook(assetId, first);
        service.WaitForIdle();
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;
        const EditorMaterialGraphCookResult undone = service.LatestResult(assetId);
        Require(undone.status == EditorMaterialGraphCookStatus::UpToDate && undone.compiledPassCount == 0U &&
                undone.cacheHitPassCount == undone.passes.size(),
            "P4.5: undoing to an earlier graph must be served from the cache without compiling");
        Require(elapsed < std::chrono::milliseconds(config.debounceMs),
            "P4.5: undoing to an earlier graph must not wait out the edit delay");
        Require(undone.elapsedMs < 400U, "P4.5: a cache hit must cost milliseconds, not the cost of a compile");
    }
    {
        // Reopening: a new service (a new editor session) finds everything in the cache on disk.
        EditorMaterialGraphCookService reopened{ config };
        const auto startedAt = std::chrono::steady_clock::now();
        const EditorMaterialGraphCookResult again = reopened.CookNow(assetId, second);
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;
        Require(again.status == EditorMaterialGraphCookStatus::UpToDate && again.compiledPassCount == 0U,
            "P4.5: reopening a graph must not compile it again");
        Require(elapsed < std::chrono::milliseconds(400), "P4.5: reopening a cooked graph must be instant");
    }
}

void RunParameterEditNeedsNoCompileTest() {
    // A scalar, vector or color parameter is a uniform: its default is a value the program is given, so
    // changing it changes neither the generated code nor the identity of the cook.
    struct Kind { RenderMaterialGraphNodeKind kind; const char* pin; const char* before; const char* after; };
    const std::array<Kind, 3U> kinds{ {
        { RenderMaterialGraphNodeKind::ParameterScalar, "value", "0.25", "0.75" },
        { RenderMaterialGraphNodeKind::ParameterVector, "xyz", "0.1 0.2 0.3", "0.9 0.8 0.7" },
        { RenderMaterialGraphNodeKind::ParameterColor, "rgba", "0.1 0.2 0.3 1", "0.9 0.8 0.7 1" },
    } };
    for (const Kind& kind : kinds) {
        const RenderMaterialAssetData before = MakeParameterArithmeticMaterial(kind.kind, kind.pin, kind.before);
        const RenderMaterialAssetData after = MakeParameterArithmeticMaterial(kind.kind, kind.pin, kind.after);
        const kb::assets::AssetId probe{ 0x7A04U };
        const kb::render::RenderMaterialGraphCompileResult beforeCompile =
            kb::render::CompileRenderMaterialGraphToShaderSource(before.graph, kb::render::RenderMaterialGraphBuildContext{ .assetId = probe.value });
        const kb::render::RenderMaterialGraphCompileResult afterCompile =
            kb::render::CompileRenderMaterialGraphToShaderSource(after.graph, kb::render::RenderMaterialGraphBuildContext{ .assetId = probe.value });
        Require(beforeCompile.Succeeded() && afterCompile.Succeeded(),
            "P4.5: the parameter test graphs must compile");
        Require(beforeCompile.shader.sourceHash == afterCompile.shader.sourceHash && beforeCompile.shader.source == afterCompile.shader.source,
            "P4.5: a parameter default must not appear in the generated shader code");
        bool defaultTravelsAsUniform = false;
        for (std::size_t index = 0U; index < beforeCompile.shader.reflection.uniforms.size() &&
                                     index < afterCompile.shader.reflection.uniforms.size(); ++index) {
            defaultTravelsAsUniform = defaultTravelsAsUniform ||
                beforeCompile.shader.reflection.uniforms[index].defaultValue != afterCompile.shader.reflection.uniforms[index].defaultValue;
        }
        Require(defaultTravelsAsUniform, "P4.5: a parameter default must reach the program as a uniform value");
    }

    // The service agrees: the same cook identity, so asking again costs nothing.
    const RenderMaterialAssetData before = MakeParameterArithmeticMaterial(RenderMaterialGraphNodeKind::ParameterColor, "rgba", "0.1 0.2 0.3 1");
    const RenderMaterialAssetData after = MakeParameterArithmeticMaterial(RenderMaterialGraphNodeKind::ParameterColor, "rgba", "0.9 0.8 0.7 1");
    EditorMaterialGraphCookConfig asyncConfig = MakeCookConfig("p45_parameter_async");
    asyncConfig.debounceMs = 10U;
    std::error_code error;
    std::filesystem::remove_all(asyncConfig.cacheRoot, error);
    EditorMaterialGraphCookService service{ asyncConfig };
    const kb::assets::AssetId assetId{ 0x7A05U };
    const std::uint64_t firstGeneration = service.RequestCook(assetId, before);
    service.WaitForIdle();
    Require(service.LatestResult(assetId).status == EditorMaterialGraphCookStatus::Ready,
        "P4.5: the parameter graph must cook");
    static_cast<void>(service.DrainResults());
    const std::uint64_t secondGeneration = service.RequestCook(assetId, after);
    Require(secondGeneration == firstGeneration,
        "P4.5: changing only a parameter default must not start a new cook");
    service.WaitForIdle();
    Require(service.DrainResults().empty() && service.LatestResult(assetId).status == EditorMaterialGraphCookStatus::Ready,
        "P4.5: changing only a parameter default must leave the cooked program as it is");

    // A constant is part of the code, so it still is a new shader.
    Require(!(service.CookNow(assetId, MakeConstantColorMaterial("0.1 0.1 0.1 1")).variantKey ==
              service.CookNow(assetId, MakeConstantColorMaterial("0.2 0.2 0.2 1")).variantKey),
        "P4.5: changing a constant, which is shader code, must still be a new cook");
}

void RunCookErrorsNameTheirNodeTest() {
    const EditorMaterialGraphCookConfig config = MakeCookConfig("p45_errors");
    EditorMaterialGraphCookService service{ config };
    const kb::assets::AssetId assetId{ 0x7A06U };
    const EditorMaterialGraphCookResult result = service.CookNow(assetId, MakeScalarIntoColorMaterial());
    Require(result.status == EditorMaterialGraphCookStatus::Failed, "P4.5: a scalar wired into a color slot must fail the cook");
    Require(result.diagnosticNodeIds.size() == result.diagnostics.size(),
        "P4.5: every cook diagnostic line must say which node it is about, or none");
    Require(std::ranges::find(result.diagnosticNodeIds, 1U) != result.diagnosticNodeIds.end() && !result.nodeDiagnostics.empty() &&
            result.nodeDiagnostics.front().nodeId == 1U,
        "P4.5: the cook must report the node the error belongs to");

    // The editor puts the error on that node and lets a click on its line reach it.
    RenderMaterialAssetData valid = MakeConstantColorMaterial("1 1 1 1");
    kb::editor::MaterialEditorState editor;
    editor.Open(assetId, valid);
    editor.ApplyCookResult(result.diagnostics, false, false, false, true, result.diagnosticNodeIds, result.nodeDiagnostics);
    const std::vector<std::uint32_t>& nodeIds = editor.DiagnosticNodeIds();
    Require(nodeIds.size() == editor.Diagnostics().size() && std::ranges::find(nodeIds, 1U) != nodeIds.end(),
        "P4.5: the diagnostics list must point at the node a compile error names");
    Require(std::ranges::any_of(editor.GraphDiagnosticMarkers(), [](const kb::editor::MaterialEditorGraphDiagnosticMarker& marker) {
            return marker.nodeId == 1U && marker.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
        }),
        "P4.5: a compile error must be marked on the node that caused it");
    editor.ApplyCookResult({}, true, true, false, false);
    Require(std::ranges::none_of(editor.GraphDiagnosticMarkers(), [](const kb::editor::MaterialEditorGraphDiagnosticMarker& marker) {
            return marker.nodeId == 1U;
        }),
        "P4.5: a later successful cook must clear the marks of the earlier failure");
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
    RunParallelPassCompilesAreDeterministicTest();
    RunStaleCompilesAreCancelledTest();
    RunReopenAndUndoHitTheCacheTest();
    RunParameterEditNeedsNoCompileTest();
    RunCookErrorsNameTheirNodeTest();
#endif
}

} // namespace kb::editor::tests
