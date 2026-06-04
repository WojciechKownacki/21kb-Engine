#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

enum class VisualGraphDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class VisualGraphDiagnosticStage : std::uint8_t {
    AssetLoad,
    Validation,
    Compile,
    NativeCodegen,
    GeneratedCodeWrite,
    Runtime,
};

struct VisualGraphDiagnostic {
    VisualGraphDiagnosticSeverity severity = VisualGraphDiagnosticSeverity::Error;
    VisualGraphDiagnosticStage stage = VisualGraphDiagnosticStage::Compile;
    std::uint32_t nodeId = 0;
    std::string pinName;
    std::string message;
};

class VisualGraphDiagnostics {
public:
    VisualGraphDiagnostics() = delete;

    [[nodiscard]] static VisualGraphDiagnostic Error(VisualGraphDiagnosticStage stage, std::string message);
    [[nodiscard]] static VisualGraphDiagnostic Error(VisualGraphDiagnosticStage stage, std::uint32_t nodeId, std::string message);
    [[nodiscard]] static VisualGraphDiagnostic Error(VisualGraphDiagnosticStage stage, std::uint32_t nodeId, std::string pinName, std::string message);
    [[nodiscard]] static std::vector<VisualGraphDiagnostic> FromErrors(VisualGraphDiagnosticStage stage, const std::vector<std::string>& errors);
    [[nodiscard]] static bool HasErrors(const std::vector<VisualGraphDiagnostic>& diagnostics) noexcept;
};

[[nodiscard]] const char* ToString(VisualGraphDiagnosticSeverity severity) noexcept;
[[nodiscard]] const char* ToString(VisualGraphDiagnosticStage stage) noexcept;

} // namespace kb::visual
