#include "engine/visual/VisualGraphBuildPipeline.hpp"

#include <utility>

namespace kb::visual {

VisualGraphBuildResult VisualGraphBuildPipeline::BuildNative(const VisualGraphAsset& graph, const VisualGraphBuildDesc& desc) {
    VisualGraphBuildResult result{};
    VisualGraphCompileResult compiled = VisualGraphCompiler::Compile(graph);
    if (!compiled.Succeeded()) {
        result.errors = std::move(compiled.errors);
        result.diagnostics = std::move(compiled.diagnostics);
        return result;
    }

    result.module = std::move(compiled.module);
    result.nativeCode = VisualGraphNativeCodeGenerator::Generate(result.module, desc.nativeCodegen);
    if (!result.nativeCode.Succeeded()) {
        result.errors = result.nativeCode.errors;
        result.diagnostics = result.nativeCode.diagnostics.empty() ? VisualGraphDiagnostics::FromErrors(VisualGraphDiagnosticStage::NativeCodegen, result.errors)
                                                                   : result.nativeCode.diagnostics;
    }
    return result;
}

} // namespace kb::visual
