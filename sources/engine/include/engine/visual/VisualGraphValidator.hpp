#pragma once

#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphValidationResult {
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphValidator {
public:
    VisualGraphValidator() = delete;

    [[nodiscard]] static VisualGraphValidationResult Validate(const VisualGraphAsset& graph);
};

} // namespace kb::visual
