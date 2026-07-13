#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <string_view>

namespace kb::render {

RenderMaterialGraphArtifactDecision ResolveRenderMaterialGraphArtifactDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state) noexcept {
    if (state.compileState == RenderMaterialGraphArtifactCompileState::Ready && state.currentArtifactAssetId != 0U) {
        return RenderMaterialGraphArtifactDecision{
            .kind = RenderMaterialGraphArtifactDecisionKind::UseCurrentArtifact,
            .artifactAssetId = state.currentArtifactAssetId,
        };
    }

    if (graph.artifactFailurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial &&
        graph.lastGoodArtifact.IsValid()) {
        return RenderMaterialGraphArtifactDecision{
            .kind = RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact,
            .artifactAssetId = graph.lastGoodArtifact.assetId,
        };
    }

    return RenderMaterialGraphArtifactDecision{
        .kind = RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial,
        .artifactAssetId = 0U,
    };
}

bool RenderMaterialGraphArtifactRuntimeDecision::UsesFallback() const noexcept {
    return decision.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact ||
        decision.kind == RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial;
}

RenderMaterialGraphArtifactRuntimeDecision ResolveRenderMaterialGraphArtifactRuntimeDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphArtifactRuntimeDecision runtime{};
    runtime.decision = ResolveRenderMaterialGraphArtifactDecision(graph, state);
    if (runtime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseCurrentArtifact) {
        return runtime;
    }

    const bool lastGood = runtime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact;
    runtime.diagnostic = RenderMaterialGraphDiagnostic{
        .severity = lastGood ? RenderMaterialGraphDiagnosticSeverity::Warning : RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .assetId = context.assetId,
        .sourcePath = context.sourcePath,
        .message = lastGood
            ? "Material graph compile failed or is pending; using the last-good shader artifact."
            : "Material graph compile failed or is pending and no last-good artifact is available; using the explicit error material.",
    };
    return runtime;
}

RenderMaterialGraphRuntimeState ResolveRenderMaterialGraphRuntimeState(const RenderMaterialGraphRuntimeStateInput& input) noexcept {
    switch (input.phase) {
    case RenderMaterialGraphCompilePhase::Editing:
        return RenderMaterialGraphRuntimeState::Dirty;
    case RenderMaterialGraphCompilePhase::Validating:
        return RenderMaterialGraphRuntimeState::Validating;
    case RenderMaterialGraphCompilePhase::Compiling:
        return RenderMaterialGraphRuntimeState::Compiling;
    case RenderMaterialGraphCompilePhase::Compiled:
        break;
    }

    const bool succeeded = input.validationSucceeded && input.compileSucceeded && input.hasGpuProgram;
    if (succeeded) {
        return RenderMaterialGraphRuntimeState::UsingGpuGraph;
    }
    if (!input.fallbackApplied) {
        return RenderMaterialGraphRuntimeState::CompileFailed;
    }
    if (input.failurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial && input.hasLastGood) {
        return RenderMaterialGraphRuntimeState::UsingLastGood;
    }
    return RenderMaterialGraphRuntimeState::UsingErrorMaterial;
}

std::string_view RenderMaterialGraphRuntimeStateName(RenderMaterialGraphRuntimeState state) noexcept {
    switch (state) {
    case RenderMaterialGraphRuntimeState::Dirty:
        return "Dirty";
    case RenderMaterialGraphRuntimeState::Validating:
        return "Validating";
    case RenderMaterialGraphRuntimeState::Compiling:
        return "Compiling";
    case RenderMaterialGraphRuntimeState::CompileFailed:
        return "CompileFailed";
    case RenderMaterialGraphRuntimeState::UsingLastGood:
        return "UsingLastGood";
    case RenderMaterialGraphRuntimeState::UsingErrorMaterial:
        return "UsingErrorMaterial";
    case RenderMaterialGraphRuntimeState::UsingGpuGraph:
        return "UsingGpuGraph";
    }
    return "Dirty";
}

bool RenderMaterialGraphRuntimeStateUsesFallback(RenderMaterialGraphRuntimeState state) noexcept {
    return state == RenderMaterialGraphRuntimeState::UsingLastGood ||
        state == RenderMaterialGraphRuntimeState::UsingErrorMaterial ||
        state == RenderMaterialGraphRuntimeState::CompileFailed;
}

bool HasGraphAuthoringData(const RenderMaterialGraphDocument& graph) noexcept {
    if (!graph.links.empty() || !graph.comments.empty() || !graph.composites.empty()) {
        return true;
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        const bool isImplicitDefault = node.id == 1U &&
            node.kind == RenderMaterialGraphNodeKind::MaterialOutput &&
            node.positionX == 640 &&
            node.positionY == 240 &&
            node.parameter.stableId.empty() &&
            node.parameter.displayName.empty();
        if (!isImplicitDefault) {
            return true;
        }
    }
    return false;
}

} // namespace kb::render
