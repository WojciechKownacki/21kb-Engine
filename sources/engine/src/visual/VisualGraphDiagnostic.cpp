#include "engine/visual/VisualGraphDiagnostic.hpp"

#include <utility>

namespace kb::visual {

VisualGraphDiagnostic VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage stage, std::string message) {
    return VisualGraphDiagnostic{
        .severity = VisualGraphDiagnosticSeverity::Error,
        .stage = stage,
        .message = std::move(message),
    };
}

VisualGraphDiagnostic VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage stage, std::uint32_t nodeId, std::string message) {
    return VisualGraphDiagnostic{
        .severity = VisualGraphDiagnosticSeverity::Error,
        .stage = stage,
        .nodeId = nodeId,
        .message = std::move(message),
    };
}

VisualGraphDiagnostic VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage stage, std::uint32_t nodeId, std::string pinName, std::string message) {
    return VisualGraphDiagnostic{
        .severity = VisualGraphDiagnosticSeverity::Error,
        .stage = stage,
        .nodeId = nodeId,
        .pinName = std::move(pinName),
        .message = std::move(message),
    };
}

std::vector<VisualGraphDiagnostic> VisualGraphDiagnostics::FromErrors(VisualGraphDiagnosticStage stage, const std::vector<std::string>& errors) {
    std::vector<VisualGraphDiagnostic> diagnostics;
    diagnostics.reserve(errors.size());
    for (const std::string& error : errors) {
        diagnostics.push_back(Error(stage, error));
    }
    return diagnostics;
}

bool VisualGraphDiagnostics::HasErrors(const std::vector<VisualGraphDiagnostic>& diagnostics) noexcept {
    for (const VisualGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == VisualGraphDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

const char* ToString(VisualGraphDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case VisualGraphDiagnosticSeverity::Info:
        return "Info";
    case VisualGraphDiagnosticSeverity::Warning:
        return "Warning";
    case VisualGraphDiagnosticSeverity::Error:
        return "Error";
    }
    return "Error";
}

const char* ToString(VisualGraphDiagnosticStage stage) noexcept {
    switch (stage) {
    case VisualGraphDiagnosticStage::AssetLoad:
        return "AssetLoad";
    case VisualGraphDiagnosticStage::Validation:
        return "Validation";
    case VisualGraphDiagnosticStage::Compile:
        return "Compile";
    case VisualGraphDiagnosticStage::NativeCodegen:
        return "NativeCodegen";
    case VisualGraphDiagnosticStage::GeneratedCodeWrite:
        return "GeneratedCodeWrite";
    case VisualGraphDiagnosticStage::Runtime:
        return "Runtime";
    }
    return "Compile";
}

} // namespace kb::visual
