#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
    case RenderMaterialGraphDiagnosticKind::UnusedParameterValue:
        return "unused_parameter_value";
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

std::string_view RenderMaterialGraphNoiseFunctionName(RenderMaterialGraphNoiseFunction function) noexcept {
    switch (function) {
    case RenderMaterialGraphNoiseFunction::Simplex:
        return "simplex";
    case RenderMaterialGraphNoiseFunction::Gradient:
        return "gradient";
    case RenderMaterialGraphNoiseFunction::Value:
        return "value";
    case RenderMaterialGraphNoiseFunction::Voronoi:
        return "voronoi";
    }
    return "simplex";
}

std::optional<RenderMaterialGraphNoiseFunction> ParseRenderMaterialGraphNoiseFunction(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "simplex")) return RenderMaterialGraphNoiseFunction::Simplex;
    if (EqualsIgnoreCase(text, "gradient")) return RenderMaterialGraphNoiseFunction::Gradient;
    if (EqualsIgnoreCase(text, "value")) return RenderMaterialGraphNoiseFunction::Value;
    if (EqualsIgnoreCase(text, "voronoi")) return RenderMaterialGraphNoiseFunction::Voronoi;
    return std::nullopt;
}

RenderMaterialGraphNoiseSettingsParseResult ParseRenderMaterialGraphNoiseSettings(std::string_view hint) {
    RenderMaterialGraphNoiseSettingsParseResult result;
    RenderMaterialGraphNoiseSettings settings;
    std::istringstream stream{ std::string{ hint } };
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) {
        tokens.push_back(token);
    }
    if (tokens.size() > 8U) {
        result.error = "Noise settings take at most 8 fields, got " + std::to_string(tokens.size()) + " in '" + std::string{ hint } + "'.";
        return result;
    }
    if (!tokens.empty()) {
        const std::optional<RenderMaterialGraphNoiseFunction> function = ParseRenderMaterialGraphNoiseFunction(tokens[0]);
        if (!function.has_value()) {
            result.error = "Unknown Noise function '" + tokens[0] + "' (expected simplex, gradient, value or voronoi).";
            return result;
        }
        settings.function = *function;
    }
    if (tokens.size() > 1U) {
        if (EqualsIgnoreCase(tokens[1], "true")) {
            settings.turbulence = true;
        } else if (EqualsIgnoreCase(tokens[1], "false")) {
            settings.turbulence = false;
        } else {
            result.error = "Noise turbulence must be true or false, got '" + tokens[1] + "'.";
            return result;
        }
    }
    static constexpr const char* kNumericNames[] = { "Scale", "Quality", "Levels", "Level Scale", "Output Min", "Output Max" };
    float numbers[6] = { settings.scale, static_cast<float>(settings.quality), static_cast<float>(settings.levels),
        settings.levelScale, settings.outputMin, settings.outputMax };
    for (std::size_t index = 2U; index < tokens.size(); ++index) {
        if (!ParseFiniteMaterialFloatToken(tokens[index], numbers[index - 2U])) {
            result.error = std::string{ "Noise " } + kNumericNames[index - 2U] + " must be a finite number, got '" + tokens[index] + "'.";
            return result;
        }
    }
    const auto isWhole = [](float value) { return std::floor(value) == value; };
    if (!(numbers[0] > 0.0F)) {
        result.error = "Noise Scale must be above zero, got " + tokens[2] + ".";
        return result;
    }
    if (!isWhole(numbers[1]) || numbers[1] < 1.0F || numbers[1] > 4.0F) {
        result.error = "Noise Quality must be a whole number from 1 to 4, got " + tokens[3] + ".";
        return result;
    }
    if (!isWhole(numbers[2]) || numbers[2] < 1.0F || numbers[2] > 10.0F) {
        result.error = "Noise Levels must be a whole number from 1 to 10, got " + tokens[4] + ".";
        return result;
    }
    if (!(numbers[3] > 0.0F)) {
        result.error = "Noise Level Scale must be above zero, got " + tokens[5] + ".";
        return result;
    }
    settings.scale = numbers[0];
    settings.quality = static_cast<std::uint32_t>(numbers[1]);
    settings.levels = static_cast<std::uint32_t>(numbers[2]);
    settings.levelScale = numbers[3];
    settings.outputMin = numbers[4];
    settings.outputMax = numbers[5];
    result.settings = settings;
    return result;
}

std::string RenderMaterialGraphNoiseSettingsHint(const RenderMaterialGraphNoiseSettings& settings) {
    std::ostringstream output;
    output << RenderMaterialGraphNoiseFunctionName(settings.function) << ' ' << (settings.turbulence ? "true" : "false") << ' '
           << settings.scale << ' ' << settings.quality << ' ' << settings.levels << ' ' << settings.levelScale << ' '
           << settings.outputMin << ' ' << settings.outputMax;
    return output.str();
}

} // namespace kb::render
