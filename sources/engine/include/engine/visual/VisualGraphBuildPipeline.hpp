#pragma once

#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphBuildDesc {
    VisualGraphNativeCodegenDesc nativeCodegen;
};

struct VisualGraphBuildResult {
    VisualGraphIrModule module;
    VisualGraphNativeCode nativeCode;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphBuildPipeline {
public:
    VisualGraphBuildPipeline() = delete;

    [[nodiscard]] static VisualGraphBuildResult BuildNative(const VisualGraphAsset& graph, const VisualGraphBuildDesc& desc);
};

} // namespace kb::visual
