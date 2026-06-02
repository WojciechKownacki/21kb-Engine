#include "RendererTestSupport.hpp"

#include "kb/render/ViewIdPolicy.hpp"
#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderFrameState.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"

#include <array>
#include <span>

namespace kb::render::tests {
namespace {

[[nodiscard]] RenderFrameDesc OneViewportFrame() {
    RenderFrameDesc frame{};
    frame.frameIndex = 42U;
    frame.viewports.push_back(RenderViewportDesc{
        .id = RenderViewportId{ 1U },
        .extent = RenderExtent{ 1280U, 720U },
        .viewportIndex = 0U,
    });
    return frame;
}

void RenderPassGraphRejectsDuplicateKindsAndViews() {
    RenderPassGraph graph;
    Require(graph.AddPass(RenderPassDesc::Logical(RenderPassKind::SceneTargetSetup)), "RenderPassGraph rejected the first logical pass");
    Require(!graph.AddPass(RenderPassDesc::Logical(RenderPassKind::SceneTargetSetup)), "RenderPassGraph accepted duplicate pass kind");
    Require(graph.AddPass(RenderPassDesc::BgfxView(RenderPassKind::OpaqueScene, ViewId::Scene3D)), "RenderPassGraph rejected valid bgfx view pass");
    Require(!graph.AddPass(RenderPassDesc::BgfxView(RenderPassKind::PostProcessBloomPrefilter, ViewId::Scene3D)), "RenderPassGraph accepted duplicate bgfx view id");
    Require(!graph.AddPass(RenderPassDesc::BgfxView(RenderPassKind::PostProcessBloomPrefilter, ViewId::Invalid)), "RenderPassGraph accepted invalid bgfx view id");
}

void RenderPassGraphValidatesRequiredPasses() {
    RenderPassGraph graph = RenderFramePipeline{}.BuildViewportGraph(RenderViewportViewIdAllocator::ForViewportIndex(0U));
    const RenderPassGraphValidationResult result = graph.ValidateRequiredPasses();
    Require(result.Succeeded(), RenderPassGraphValidationStatusName(result.status));
    Require(graph.Resources().size() == 9U, "RenderPassGraph did not declare the viewport graph resources");
    Require(graph.FindResource(RenderGraphResource::SceneColor) != nullptr, "RenderPassGraph did not declare SceneColor");
    Require(graph.FindResource(RenderGraphResource::ShadowMap) != nullptr, "RenderPassGraph did not declare ShadowMap");
    Require(graph.FindResource(RenderGraphResource::FinalOutput) != nullptr, "RenderPassGraph did not declare FinalOutput");
    Require(RequiredRenderPassKinds().size() == RenderPassKindCount, "Required render pass list does not match pass kind count");
    for (const RenderPassKind kind : RequiredRenderPassKinds()) {
        Require(graph.HasPass(kind), "RenderPassGraph required pass was not present");
    }

    const RenderPassGraphCompileResult compile = graph.Compile();
    Require(compile.Succeeded(), RenderPassGraphValidationStatusName(compile.validation.status));
    Require(compile.resourceUsages.size() == graph.Resources().size(), "RenderPassGraph compile did not emit usage for every resource");
    Require(compile.externalResourceCount == 4U, "RenderPassGraph compile reported wrong external resource count");
    Require(compile.transientResourceCount == 5U, "RenderPassGraph compile reported wrong transient resource count");
    Require(compile.estimatedTransientBytes > 0U, "RenderPassGraph compile did not estimate transient memory");
}

void RenderPassGraphValidatorReportsMissingDisabledAndInvalidPasses() {
    RenderPassGraph missing;
    Require(missing.AddPass(RenderPassDesc::Logical(RenderPassKind::SceneTargetSetup)), "RenderPassGraph rejected minimal logical pass");
    RenderPassGraphValidationResult missingResult = missing.ValidateRequiredPasses();
    Require(missingResult.status == RenderPassGraphValidationStatus::MissingRequiredPass, "RenderPassGraph did not report missing required pass");
    Require(missingResult.pass == RenderPassKind::ShadowDepth, "RenderPassGraph reported the wrong missing pass");

    RenderPassGraph source = RenderFramePipeline{}.BuildViewportGraph(RenderViewportViewIdAllocator::ForViewportIndex(0U));
    RenderPassGraph disabled;
    for (const RenderGraphResourceDesc& resource : source.Resources()) {
        Require(disabled.AddResource(resource), "RenderPassGraph failed to copy disabled-pass test resource");
    }
    for (const RenderPassDesc& sourcePass : source.Passes()) {
        RenderPassDesc pass = sourcePass;
        if (pass.kind == RenderPassKind::FinalComposite) {
            pass.enabled = false;
        }
        Require(disabled.AddPass(pass), "RenderPassGraph failed to rebuild disabled-pass graph");
    }
    const RenderPassGraphValidationResult disabledResult = disabled.ValidateRequiredPasses();
    Require(disabledResult.status == RenderPassGraphValidationStatus::DisabledRequiredPass, "RenderPassGraph did not report disabled required pass");
    Require(disabledResult.pass == RenderPassKind::FinalComposite, "RenderPassGraph reported the wrong disabled pass");

    RenderPassGraph invalidId;
    RenderPassDesc invalid = RenderPassDesc::Logical(RenderPassKind::SceneTargetSetup);
    invalid.id = RenderPassId{};
    Require(invalidId.AddPass(invalid), "RenderPassGraph rejected invalid-id pass before validation");
    const RenderPassGraphValidationResult invalidResult = invalidId.ValidateRequiredPasses();
    Require(invalidResult.status == RenderPassGraphValidationStatus::InvalidPassId, "RenderPassGraph did not report invalid pass id");
}

void RenderPassGraphValidatorReportsResourceDependencyErrors() {
    RenderPassGraph missingResource;
    Require(missingResource.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::SceneColor,
        .target = RenderTargetDesc{
            .role = RenderTargetRole::SceneColorHdr,
            .format = RenderTargetFormat::Rgba16F,
            .extent = RenderExtent{ 320U, 200U },
        },
        .lifetime = RenderGraphResourceLifetime::External,
    }), "RenderPassGraph rejected valid resource declaration");
    RenderPassDesc missingResourcePass = RenderPassDesc::BgfxView(RenderPassKind::OpaqueScene, ViewId::Scene3D);
    missingResourcePass.Writes(RenderGraphResource::SceneDepth);
    Require(missingResource.AddPass(missingResourcePass), "RenderPassGraph rejected missing-resource test pass");
    const RenderPassGraphValidationResult missingResourceResult = missingResource.ValidateRequiredPasses();
    Require(missingResourceResult.status == RenderPassGraphValidationStatus::MissingResourceDeclaration, "RenderPassGraph did not report missing resource declaration");
    Require(missingResourceResult.resource == RenderGraphResource::SceneDepth, "RenderPassGraph reported the wrong missing resource");

    RenderPassGraph passWithoutOutput;
    RenderPassDesc noOutput = RenderPassDesc::BgfxView(RenderPassKind::OpaqueScene, ViewId::Scene3D);
    Require(passWithoutOutput.AddPass(noOutput), "RenderPassGraph rejected no-output test pass");
    const RenderPassGraphValidationResult noOutputResult = passWithoutOutput.ValidateRequiredPasses();
    Require(noOutputResult.status == RenderPassGraphValidationStatus::PassWithoutOutput, "RenderPassGraph did not report cullable pass without output");

    RenderPassGraph readBeforeWrite;
    Require(readBeforeWrite.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::BloomPrefilter,
        .target = RenderTargetDesc{
            .role = RenderTargetRole::PostProcessColor,
            .format = RenderTargetFormat::Rgba16F,
            .extent = RenderExtent{ 320U, 200U },
            .sampled = true,
        },
        .lifetime = RenderGraphResourceLifetime::Transient,
    }), "RenderPassGraph rejected read-before-write resource");
    RenderPassDesc readPass = RenderPassDesc::BgfxView(RenderPassKind::PostProcessBloomBlurH, ViewId::PostProcessBloomBlurH);
    readPass.Reads(RenderGraphResource::BloomPrefilter).Writes(RenderGraphResource::BloomPrefilter);
    Require(readBeforeWrite.AddPass(readPass), "RenderPassGraph rejected read-before-write pass");
    const RenderPassGraphValidationResult readBeforeWriteResult = readBeforeWrite.ValidateRequiredPasses();
    Require(readBeforeWriteResult.status == RenderPassGraphValidationStatus::ReadBeforeWrite, "RenderPassGraph did not report transient read before write");
}

void PrimaryViewportIdsUseCanonicalRuntimeViews() {
    const RenderViewportViewIds ids = RenderViewportViewIdAllocator::ForViewportIndex(0U);
    Require(ids.IsValid(), "Primary viewport view ids are invalid");
    Require(ids.shadowDepth == ViewId::ShadowDepth, "Primary shadow depth view id is not canonical");
    Require(ids.opaqueScene == ViewId::Scene3D, "Primary opaque scene view id is not Scene3D");
    Require(ids.transparentScene == ViewId::TransparentScene, "Primary transparent scene view id is not canonical");
    Require(ids.selectionMask == ViewId::EditorSelectionMask, "Primary selection mask view id is not canonical");
    Require(ids.postProcessBloomPrefilter == ViewId::PostProcessBloomPrefilter, "Primary bloom prefilter view id is not canonical");
    Require(ids.postProcessBloomBlurH == ViewId::PostProcessBloomBlurH, "Primary bloom blur H view id is not canonical");
    Require(ids.postProcessBloomBlurV == ViewId::PostProcessBloomBlurV, "Primary bloom blur V view id is not canonical");
    Require(ids.postProcessHdrCombine == ViewId::PostProcessHdrCombine, "Primary HDR combine view id is not canonical");
    Require(ids.postProcessHdrFinalize == ViewId::PostProcessHdrFinalize, "Primary HDR finalize view id is not canonical");
    Require(ids.sceneOverlays == ViewId::Overlay, "Primary overlay view id is not canonical");
    Require(ids.finalComposite == ViewId::FinalComposite, "Primary final composite view id is not canonical");
    Require(ids.editorUiComposite == ViewId::EditorUi, "Primary editor UI view id is not canonical");
}

void DetachedViewportIdsAreContiguousAndBounded() {
    const RenderViewportViewIds first = RenderViewportViewIdAllocator::ForViewportIndex(1U);
    const RenderViewportViewIds second = RenderViewportViewIdAllocator::ForViewportIndex(2U);
    Require(first.IsValid(), "First detached viewport view ids are invalid");
    Require(second.IsValid(), "Second detached viewport view ids are invalid");
    Require(first.shadowDepth == ViewId::DetachedViewportStart, "First detached viewport shadow view id is wrong");
    Require(first.opaqueScene == ViewId::DetachedViewportStart + 1U, "First detached viewport scene view id is wrong");
    Require(first.transparentScene == ViewId::DetachedViewportStart + 2U, "First detached viewport transparent scene view id is wrong");
    Require(first.editorUiComposite == ViewId::DetachedViewportStart + ViewId::DetachedViewportStride - 1U, "First detached viewport stride is wrong");
    Require(second.shadowDepth == ViewId::DetachedViewportStart + ViewId::DetachedViewportStride, "Second detached viewport shadow view id is wrong");
    Require(second.opaqueScene == ViewId::DetachedViewportStart + ViewId::DetachedViewportStride + 1U, "Second detached viewport scene view id is wrong");
    Require(RenderViewportViewIdAllocator::ForViewportIndex(20U).IsValid(), "Last supported detached viewport should be valid");
    Require(!RenderViewportViewIdAllocator::ForViewportIndex(21U).IsValid(), "Out-of-range detached viewport should be invalid");
}

void FramePipelineBuildsCanonicalPassOrder() {
    const RenderFramePipeline pipeline;
    const RenderFramePlan plan = pipeline.Build(OneViewportFrame());
    Require(plan.Succeeded(), RenderFramePipelineStatusName(plan.status));
    Require(plan.viewports.size() == 1U, "RenderFramePipeline did not build one viewport plan");

    const RenderViewportPlan& viewport = plan.viewports[0];
    Require(viewport.graphValidation.Succeeded(), RenderPassGraphValidationStatusName(viewport.graphValidation.status));
    RenderPassGraph viewportGraph = RenderFramePipeline{}.BuildViewportGraph(viewport.viewIds, viewport.viewport.extent);
    const RenderGraphResourceDesc* sceneColor = viewportGraph.FindResource(RenderGraphResource::SceneColor);
    Require(sceneColor != nullptr && sceneColor->target.extent == viewport.viewport.extent, "RenderFramePipeline did not preserve viewport extent in graph resources");
    Require(viewport.graphCompile.Succeeded(), RenderPassGraphValidationStatusName(viewport.graphCompile.validation.status));
    Require(viewport.graphCompile.estimatedTransientBytes >= 1280ULL * 720ULL, "RenderFramePipeline did not carry compiled transient memory estimate");
    constexpr std::array<RenderPassKind, RenderPassKindCount> expectedPasses{
        RenderPassKind::SceneTargetSetup,
        RenderPassKind::ShadowDepth,
        RenderPassKind::OpaqueScene,
        RenderPassKind::TransparentScene,
        RenderPassKind::EditorSelectionMask,
        RenderPassKind::PostProcessBloomPrefilter,
        RenderPassKind::PostProcessBloomBlurH,
        RenderPassKind::PostProcessBloomBlurV,
        RenderPassKind::PostProcessHdrCombine,
        RenderPassKind::PostProcessHdrFinalize,
        RenderPassKind::EditorSceneOverlays,
        RenderPassKind::FinalComposite,
        RenderPassKind::EditorUiComposite,
    };

    Require(viewport.passes.size() == expectedPasses.size(), "RenderFramePipeline pass count is wrong");
    for (std::size_t index = 0; index < expectedPasses.size(); ++index) {
        Require(viewport.passes[index].kind == expectedPasses[index], "RenderFramePipeline pass order is wrong");
    }
    const RenderPassDesc* opaqueScene = viewportGraph.FindPass(RenderPassKind::OpaqueScene);
    const RenderPassDesc* shadowDepth = viewportGraph.FindPass(RenderPassKind::ShadowDepth);
    const RenderPassDesc* transparentScene = viewportGraph.FindPass(RenderPassKind::TransparentScene);
    const RenderPassDesc* selectionMask = viewportGraph.FindPass(RenderPassKind::EditorSelectionMask);
    const RenderPassDesc* finalComposite = viewportGraph.FindPass(RenderPassKind::FinalComposite);
    Require(shadowDepth != nullptr && shadowDepth->meshPass.value_or(MeshPassType::Depth) == MeshPassType::ShadowDepth, "ShadowDepth pass did not carry ShadowDepth mesh pass metadata");
    Require(opaqueScene != nullptr && opaqueScene->meshPass.value_or(MeshPassType::Depth) == MeshPassType::BaseOpaque, "OpaqueScene pass did not carry BaseOpaque mesh pass metadata");
    Require(transparentScene != nullptr && transparentScene->meshPass.value_or(MeshPassType::Depth) == MeshPassType::BaseTransparent, "TransparentScene pass did not carry BaseTransparent mesh pass metadata");
    Require(selectionMask != nullptr && selectionMask->meshPass.value_or(MeshPassType::Depth) == MeshPassType::SelectionId, "EditorSelectionMask pass did not carry SelectionId mesh pass metadata");
    Require(finalComposite != nullptr && !finalComposite->meshPass.has_value(), "FinalComposite pass unexpectedly carried mesh pass metadata");

    constexpr std::array<std::uint16_t, RenderPassKindCount - 1U> expectedViewOrder{
        ViewId::ShadowDepth,
        ViewId::Scene3D,
        ViewId::TransparentScene,
        ViewId::EditorSelectionMask,
        ViewId::PostProcessBloomPrefilter,
        ViewId::PostProcessBloomBlurH,
        ViewId::PostProcessBloomBlurV,
        ViewId::PostProcessHdrCombine,
        ViewId::PostProcessHdrFinalize,
        ViewId::Overlay,
        ViewId::FinalComposite,
        ViewId::EditorUi,
    };
    Require(viewport.viewOrder.size() == expectedViewOrder.size(), "RenderFramePipeline view order count is wrong");
    for (std::size_t index = 0; index < expectedViewOrder.size(); ++index) {
        Require(viewport.viewOrder[index] == expectedViewOrder[index], "RenderFramePipeline view order is wrong");
    }
}

void FramePipelineRejectsInvalidViewports() {
    RenderFrameDesc frame{};
    frame.viewports.push_back(RenderViewportDesc{
        .id = RenderViewportId{ 1U },
        .extent = RenderExtent{ 0U, 720U },
        .viewportIndex = 0U,
    });

    const RenderFramePlan plan = RenderFramePipeline{}.Build(frame);
    Require(!plan.Succeeded(), "RenderFramePipeline accepted an invalid viewport");
    Require(plan.status == RenderFramePipelineStatus::InvalidViewport, "RenderFramePipeline reported the wrong invalid viewport status");
}

void FrameStateAccumulatesMultipleViewportViewOrders() {
    RenderFrameDesc frame{};
    frame.frameIndex = 7U;
    frame.viewports.push_back(RenderViewportDesc{
        .id = RenderViewportId{ 1U },
        .extent = RenderExtent{ 1280U, 720U },
        .viewportIndex = 0U,
    });
    frame.viewports.push_back(RenderViewportDesc{
        .id = RenderViewportId{ 2U },
        .extent = RenderExtent{ 640U, 360U },
        .viewportIndex = 1U,
    });

    const RenderFramePlan plan = RenderFramePipeline{}.Build(frame);
    Require(plan.Succeeded(), "RenderFramePipeline did not build a multi-viewport frame plan");
    Require(plan.viewports.size() == 2U, "RenderFramePipeline built the wrong number of viewport plans");

    RenderFrameState state;
    state.Begin(frame.frameIndex);
    Require(state.RegisterViewportPlan(plan.viewports[0]), "RenderFrameState rejected the primary viewport plan");
    Require(state.RegisterViewportPlan(plan.viewports[1]), "RenderFrameState rejected the detached viewport plan");
    Require(state.IsActive(), "RenderFrameState became inactive during viewport registration");
    Require(state.FrameIndex() == frame.frameIndex, "RenderFrameState has the wrong frame index");

    constexpr std::array<std::uint16_t, 24U> expectedViewOrder{
        ViewId::ShadowDepth,
        ViewId::Scene3D,
        ViewId::TransparentScene,
        ViewId::EditorSelectionMask,
        ViewId::PostProcessBloomPrefilter,
        ViewId::PostProcessBloomBlurH,
        ViewId::PostProcessBloomBlurV,
        ViewId::PostProcessHdrCombine,
        ViewId::PostProcessHdrFinalize,
        ViewId::Overlay,
        ViewId::FinalComposite,
        ViewId::EditorUi,
        ViewId::DetachedViewportStart,
        ViewId::DetachedViewportStart + 1U,
        ViewId::DetachedViewportStart + 2U,
        ViewId::DetachedViewportStart + 3U,
        ViewId::DetachedViewportStart + 4U,
        ViewId::DetachedViewportStart + 5U,
        ViewId::DetachedViewportStart + 6U,
        ViewId::DetachedViewportStart + 7U,
        ViewId::DetachedViewportStart + 8U,
        ViewId::DetachedViewportStart + 9U,
        ViewId::DetachedViewportStart + 10U,
        ViewId::DetachedViewportStart + 11U,
    };

    const std::span<const std::uint16_t> viewOrder = state.ViewOrder();
    Require(viewOrder.size() == expectedViewOrder.size(), "RenderFrameState accumulated the wrong view order count");
    for (std::size_t index = 0; index < expectedViewOrder.size(); ++index) {
        Require(viewOrder[index] == expectedViewOrder[index], "RenderFrameState accumulated the wrong view order");
    }
}

void FrameStateRejectsInvalidLifecycleAndDuplicateViews() {
    const RenderFramePlan plan = RenderFramePipeline{}.Build(OneViewportFrame());
    Require(plan.Succeeded(), "RenderFramePipeline did not build the single viewport frame plan");

    RenderFrameState state;
    Require(!state.RegisterViewportPlan(plan.viewports[0]), "RenderFrameState accepted a viewport before Begin");

    state.Begin(9U);
    Require(state.RegisterViewportPlan(plan.viewports[0]), "RenderFrameState rejected the first viewport registration");
    const std::size_t viewOrderSize = state.ViewOrder().size();
    Require(!state.RegisterViewportPlan(plan.viewports[0]), "RenderFrameState accepted duplicate bgfx view ids");
    Require(state.ViewOrder().size() == viewOrderSize, "RenderFrameState mutated view order after rejecting duplicate views");

    state.End();
    Require(!state.IsActive(), "RenderFrameState stayed active after End");
    Require(state.FrameIndex() == 0U, "RenderFrameState retained frame index after End");
    Require(state.ViewOrder().empty(), "RenderFrameState retained view order after End");
}

void SceneSubmitDescValidatesViewportTarget() {
    RenderSceneSubmitDesc desc{};
    Require(!desc.IsValid(), "Default RenderSceneSubmitDesc should be invalid");

    desc.target.viewport = RenderViewportDesc{
        .id = RenderViewportId{ 1U },
        .extent = RenderExtent{ 320U, 200U },
        .viewportIndex = 0U,
    };
    Require(desc.IsValid(), "RenderSceneSubmitDesc rejected a valid viewport target");
}

void SceneSubmitDescCarriesMeshRuntimePolicy() {
    RenderSceneSubmitDesc desc{};
    desc.drawBudget = SceneRenderDrawBudget{
        .maxDrawCommands = 8U,
        .maxVisibleInstances = 128U,
    };
    desc.lightingConfig = SceneRenderLightingConfig{
        .maxForwardLights = 3U,
        .environmentMode = SceneRenderEnvironmentMode::Hemisphere,
    };

    Require(desc.drawBudget.maxDrawCommands == 8U, "RenderSceneSubmitDesc did not preserve draw command budget");
    Require(desc.drawBudget.maxVisibleInstances == 128U, "RenderSceneSubmitDesc did not preserve visible instance budget");
    Require(desc.lightingConfig.maxForwardLights == 3U, "RenderSceneSubmitDesc did not preserve forward light budget");
    Require(desc.lightingConfig.environmentMode == SceneRenderEnvironmentMode::Hemisphere, "RenderSceneSubmitDesc did not preserve environment lighting mode");
    Require(desc.meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent, "RenderSceneSubmitDesc did not default Phase 9 mesh runtime to opaque and transparent");

    desc.target.viewport = RenderViewportDesc{
        .id = RenderViewportId{ 1U },
        .extent = RenderExtent{ 320U, 200U },
        .viewportIndex = 0U,
    };
    desc.meshPassMode = SceneRenderMeshPassMode::OpaqueOnly;
    Require(desc.IsValid(), "RenderSceneSubmitDesc rejected explicit opaque-only mesh pass mode");
    desc.meshPassMode = static_cast<SceneRenderMeshPassMode>(0xFFU);
    Require(!desc.IsValid(), "RenderSceneSubmitDesc accepted an unsupported mesh pass mode");
}

} // namespace

void RunRenderFramePipelineTests() {
    RenderPassGraphRejectsDuplicateKindsAndViews();
    RenderPassGraphValidatesRequiredPasses();
    RenderPassGraphValidatorReportsMissingDisabledAndInvalidPasses();
    RenderPassGraphValidatorReportsResourceDependencyErrors();
    PrimaryViewportIdsUseCanonicalRuntimeViews();
    DetachedViewportIdsAreContiguousAndBounded();
    FramePipelineBuildsCanonicalPassOrder();
    FramePipelineRejectsInvalidViewports();
    FrameStateAccumulatesMultipleViewportViewOrders();
    FrameStateRejectsInvalidLifecycleAndDuplicateViews();
    SceneSubmitDescValidatesViewportTarget();
    SceneSubmitDescCarriesMeshRuntimePolicy();
}

} // namespace kb::render::tests
