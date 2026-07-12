#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace kb::render {
namespace {

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index]; char right = rhs[index];
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}

} // namespace

std::string_view RenderMaterialGraphArtifactFailurePolicyName(RenderMaterialGraphArtifactFailurePolicy policy) noexcept {
    switch (policy) {
    case RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial:
        return "LastGoodThenErrorMaterial";
    case RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial:
        return "ErrorMaterial";
    }
    return "LastGoodThenErrorMaterial";
}

std::optional<RenderMaterialGraphArtifactFailurePolicy> ParseRenderMaterialGraphArtifactFailurePolicy(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "LastGoodThenErrorMaterial") || EqualsIgnoreCase(text, "LastGoodThenError")) {
        return RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    }
    if (EqualsIgnoreCase(text, "ErrorMaterial")) {
        return RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial;
    }
    return std::nullopt;
}

std::string_view RenderMaterialGraphDiagnosticKindName(RenderMaterialGraphDiagnosticKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput:
        return "disconnected_required_output";
    case RenderMaterialGraphDiagnosticKind::TypeMismatch:
        return "type_mismatch";
    case RenderMaterialGraphDiagnosticKind::Cycle:
        return "cycle";
    case RenderMaterialGraphDiagnosticKind::UnsupportedNode:
        return "unsupported_node";
    case RenderMaterialGraphDiagnosticKind::MissingTexture:
        return "missing_texture";
    case RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole:
        return "invalid_color_space_role";
    case RenderMaterialGraphDiagnosticKind::UnsupportedBlendMode:
        return "unsupported_blend_mode";
    case RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed:
        return "shader_generation_failed";
    case RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId:
        return "duplicate_parameter_stable_id";
    case RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode:
        return "unsupported_render_path_node";
    case RenderMaterialGraphDiagnosticKind::TextureSamplerLimitExceeded:
        return "texture_sampler_limit_exceeded";
    case RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain:
        return "unsupported_material_domain";
    case RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel:
        return "unsupported_shading_model";
    case RenderMaterialGraphDiagnosticKind::StaticPermutationExplosion:
        return "static_permutation_explosion";
    case RenderMaterialGraphDiagnosticKind::MissingSourceGraph:
        return "missing_source_graph";
    case RenderMaterialGraphDiagnosticKind::MissingMaterialFunction:
        return "missing_material_function";
    case RenderMaterialGraphDiagnosticKind::MaterialFunctionCycle:
        return "material_function_cycle";
    case RenderMaterialGraphDiagnosticKind::MaterialFunctionSignatureMismatch:
        return "material_function_signature_mismatch";
    case RenderMaterialGraphDiagnosticKind::SourceGraphLoadDiagnostic:
        return "source_graph_load_diagnostic";
    }
    return "unsupported_node";
}

std::string_view RenderMaterialGraphRenderPathName(RenderMaterialGraphRenderPath path) noexcept {
    switch (path) {
    case RenderMaterialGraphRenderPath::GpuForward:
        return "GpuForward";
    case RenderMaterialGraphRenderPath::GpuShadow:
        return "GpuShadow";
    case RenderMaterialGraphRenderPath::GpuDeferred:
        return "GpuDeferred";
    case RenderMaterialGraphRenderPath::CpuFallback:
        return "CpuFallback";
    case RenderMaterialGraphRenderPath::Preview:
        return "Preview";
    }
    return "GpuForward";
}

std::string_view RenderMaterialGraphNodeSupportName(RenderMaterialGraphNodeSupport support) noexcept {
    switch (support) {
    case RenderMaterialGraphNodeSupport::Production:
        return "Production";
    case RenderMaterialGraphNodeSupport::Experimental:
        return "Experimental";
    case RenderMaterialGraphNodeSupport::FallbackOnly:
        return "FallbackOnly";
    case RenderMaterialGraphNodeSupport::Unsupported:
        return "Unsupported";
    }
    return "Unsupported";
}

} // namespace kb::render
