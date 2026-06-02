#include "kb/render/frame/RenderFramePipeline.hpp"

#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] RenderTargetDesc Target(RenderTargetRole role, RenderTargetFormat format, RenderExtent extent, bool renderable, bool sampled) noexcept {
    return RenderTargetDesc{
        .role = role,
        .format = format,
        .extent = extent,
        .renderable = renderable,
        .sampled = sampled,
    };
}

void AddViewportResources(RenderPassGraph& graph, RenderExtent extent) {
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::SceneColor,
        .target = Target(RenderTargetRole::SceneColorHdr, RenderTargetFormat::Rgba16F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::External,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::SceneDepth,
        .target = Target(RenderTargetRole::SceneDepth, RenderTargetFormat::D32F, extent, true, false),
        .lifetime = RenderGraphResourceLifetime::External,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::ShadowMap,
        .target = Target(RenderTargetRole::SceneDepth, RenderTargetFormat::D32F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::External,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::SelectionMask,
        .target = Target(RenderTargetRole::SelectionMask, RenderTargetFormat::R8, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::Transient,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::BloomPrefilter,
        .target = Target(RenderTargetRole::PostProcessColor, RenderTargetFormat::Rgba16F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::Transient,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::BloomPing,
        .target = Target(RenderTargetRole::PostProcessColor, RenderTargetFormat::Rgba16F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::Transient,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::BloomCombine,
        .target = Target(RenderTargetRole::PostProcessColor, RenderTargetFormat::Rgba16F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::Transient,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::PostProcessFinal,
        .target = Target(RenderTargetRole::PostProcessColor, RenderTargetFormat::Rgba16F, extent, true, true),
        .lifetime = RenderGraphResourceLifetime::Transient,
    }));
    static_cast<void>(graph.AddResource(RenderGraphResourceDesc{
        .id = RenderGraphResource::FinalOutput,
        .target = Target(RenderTargetRole::CompositeOutput, RenderTargetFormat::Backbuffer, extent, true, false),
        .lifetime = RenderGraphResourceLifetime::External,
    }));
}

[[nodiscard]] RenderPassDesc BuildPass(RenderPassKind kind, const RenderViewportViewIds& viewIds) {
    RenderPassDesc pass = kind == RenderPassKind::SceneTargetSetup
        ? RenderPassDesc::Logical(kind)
        : RenderPassDesc::BgfxView(kind, viewIds.ViewFor(kind));
    switch (kind) {
    case RenderPassKind::SceneTargetSetup:
        pass.NeverCull();
        break;
    case RenderPassKind::ShadowDepth:
        pass.Writes(RenderGraphResource::ShadowMap);
        break;
    case RenderPassKind::OpaqueScene:
        pass.Reads(RenderGraphResource::ShadowMap).Writes(RenderGraphResource::SceneColor).Writes(RenderGraphResource::SceneDepth);
        break;
    case RenderPassKind::TransparentScene:
        pass.Reads(RenderGraphResource::SceneDepth).Writes(RenderGraphResource::SceneColor);
        break;
    case RenderPassKind::EditorSelectionMask:
        pass.Reads(RenderGraphResource::SceneDepth).Writes(RenderGraphResource::SelectionMask);
        break;
    case RenderPassKind::PostProcessBloomPrefilter:
        pass.Reads(RenderGraphResource::SceneColor).Writes(RenderGraphResource::BloomPrefilter);
        break;
    case RenderPassKind::PostProcessBloomBlurH:
        pass.Reads(RenderGraphResource::BloomPrefilter).Writes(RenderGraphResource::BloomPing);
        break;
    case RenderPassKind::PostProcessBloomBlurV:
        pass.Reads(RenderGraphResource::BloomPing).Writes(RenderGraphResource::BloomPrefilter);
        break;
    case RenderPassKind::PostProcessHdrCombine:
        pass.Reads(RenderGraphResource::SceneColor).Reads(RenderGraphResource::BloomPrefilter).Writes(RenderGraphResource::BloomCombine);
        break;
    case RenderPassKind::PostProcessHdrFinalize:
        pass.Reads(RenderGraphResource::BloomCombine).Writes(RenderGraphResource::PostProcessFinal);
        break;
    case RenderPassKind::EditorSceneOverlays:
        pass.Reads(RenderGraphResource::SceneDepth).Writes(RenderGraphResource::SceneColor);
        break;
    case RenderPassKind::FinalComposite:
        pass.Reads(RenderGraphResource::PostProcessFinal).Writes(RenderGraphResource::FinalOutput);
        break;
    case RenderPassKind::EditorUiComposite:
        pass.Reads(RenderGraphResource::FinalOutput).Writes(RenderGraphResource::FinalOutput);
        break;
    }
    return pass;
}

void CopyGraph(RenderPassGraph& graph, RenderViewportPlan& plan) {
    const std::span<const RenderPassDesc> passes = graph.Passes();
    plan.passes.assign(passes.begin(), passes.end());

    const std::span<const std::uint16_t> viewOrder = graph.ViewOrder();
    plan.viewOrder.assign(viewOrder.begin(), viewOrder.end());
    plan.graphValidation = graph.ValidateRequiredPasses();
    plan.graphCompile = graph.Compile();
}

} // namespace

const char* RenderFramePipelineStatusName(RenderFramePipelineStatus status) noexcept {
    switch (status) {
    case RenderFramePipelineStatus::Success:
        return "Success";
    case RenderFramePipelineStatus::EmptyFrame:
        return "EmptyFrame";
    case RenderFramePipelineStatus::InvalidViewport:
        return "InvalidViewport";
    case RenderFramePipelineStatus::ViewIdsUnavailable:
        return "ViewIdsUnavailable";
    case RenderFramePipelineStatus::InvalidPassGraph:
        return "InvalidPassGraph";
    }
    return "Unknown";
}

RenderFramePlan RenderFramePipeline::Build(const RenderFrameDesc& desc) const {
    RenderFramePlan plan{};
    if (!desc.HasViewports()) {
        plan.status = RenderFramePipelineStatus::EmptyFrame;
        return plan;
    }

    plan.viewports.reserve(desc.viewports.size());
    for (const RenderViewportDesc& viewport : desc.viewports) {
        if (!viewport.IsValid()) {
            plan.status = RenderFramePipelineStatus::InvalidViewport;
            plan.viewports.clear();
            return plan;
        }

        RenderViewportViewIds viewIds = RenderViewportViewIdAllocator::ForViewportIndex(viewport.viewportIndex);
        if (!viewIds.IsValid()) {
            plan.status = RenderFramePipelineStatus::ViewIdsUnavailable;
            plan.viewports.clear();
            return plan;
        }

        RenderPassGraph graph = BuildViewportGraph(viewIds, viewport.extent);
        const RenderPassGraphCompileResult graphCompile = graph.Compile();
        if (!graphCompile.Succeeded()) {
            plan.status = RenderFramePipelineStatus::InvalidPassGraph;
            plan.viewports.clear();
            return plan;
        }

        RenderViewportPlan viewportPlan{};
        viewportPlan.viewport = viewport;
        viewportPlan.viewIds = viewIds;
        CopyGraph(graph, viewportPlan);
        plan.viewports.push_back(std::move(viewportPlan));
    }

    plan.status = RenderFramePipelineStatus::Success;
    return plan;
}

RenderPassGraph RenderFramePipeline::BuildViewportGraph(const RenderViewportViewIds& viewIds) const {
    return BuildViewportGraph(viewIds, RenderExtent{ 1U, 1U });
}

RenderPassGraph RenderFramePipeline::BuildViewportGraph(const RenderViewportViewIds& viewIds, RenderExtent extent) const {
    RenderPassGraph graph;
    AddViewportResources(graph, extent);
    for (const RenderPassKind kind : RequiredRenderPassKinds()) {
        const RenderPassDesc pass = BuildPass(kind, viewIds);
        if (!graph.AddPass(pass)) {
            graph.Clear();
            break;
        }
    }
    return graph;
}

} // namespace kb::render
