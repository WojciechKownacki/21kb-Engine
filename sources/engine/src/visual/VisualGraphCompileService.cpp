#include "engine/visual/VisualGraphCompileService.hpp"

#include "engine/visual/VisualGraphGeneratedCodeWriter.hpp"

#include <utility>

namespace kb::visual {

VisualGraphCompileServiceResult VisualGraphCompileService::CompileAsset(
    kb::assets::AssetManager& assets,
    kb::assets::AssetId assetId,
    const VisualGraphCompileRequest& request,
    VisualGraphRuntimeRegistry& runtimeRegistry) {
    VisualGraphCompileServiceResult result{};
    if (!assetId.IsValid()) {
        result.errors.push_back("visual graph compile request has invalid asset id");
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::AssetLoad, result.errors.back()));
        return result;
    }

    const kb::assets::AssetHandle<VisualGraphAsset> graph = assets.Load<VisualGraphAsset>(assetId);
    if (!graph.IsLoaded()) {
        result.errors.push_back(assets.LastError().empty() ? "visual graph asset could not be loaded" : assets.LastError());
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::AssetLoad, result.errors.back()));
        return result;
    }

    VisualGraphBuildResult built = VisualGraphBuildPipeline::BuildNative(*graph.Get(), request.build);
    if (!built.Succeeded()) {
        result.errors = std::move(built.errors);
        result.diagnostics = std::move(built.diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics = VisualGraphDiagnostics::FromErrors(VisualGraphDiagnosticStage::Compile, result.errors);
        }
        return result;
    }

    result.artifact = VisualGraphRuntimeArtifact{
        .assetId = assetId,
        .graphName = graph->name,
        .module = std::move(built.module),
        .nativeCode = std::move(built.nativeCode),
    };

    if (request.writeGeneratedCode) {
        VisualGraphGeneratedCodeWriteResult written = VisualGraphGeneratedCodeWriter::Write(result.artifact.nativeCode, request.generatedCodeDirectory);
        if (!written.Succeeded()) {
            result.errors = std::move(written.errors);
            result.diagnostics = VisualGraphDiagnostics::FromErrors(VisualGraphDiagnosticStage::GeneratedCodeWrite, result.errors);
            return result;
        }
        result.artifact.generatedFiles = std::move(written.files);
    }

    runtimeRegistry.Store(result.artifact);
    return result;
}

} // namespace kb::visual
