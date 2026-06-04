#include "engine/visual/VisualGraphCompileCoordinator.hpp"

#include "engine/script/ScriptApiNameCollector.hpp"
#include "engine/visual/VisualGraphGeneratedCodeWriter.hpp"
#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"

#include <utility>

namespace kb::visual {
namespace {

void SetErrors(
    VisualGraphCompileCoordinatorResult& result,
    std::vector<std::string> errors,
    std::vector<VisualGraphDiagnostic> diagnostics,
    VisualGraphDiagnosticStage fallbackStage) {
    result.errors = std::move(errors);
    result.diagnostics = diagnostics.empty() ? VisualGraphDiagnostics::FromErrors(fallbackStage, result.errors) : std::move(diagnostics);
}

} // namespace

VisualGraphCompileCoordinatorResult VisualGraphCompileCoordinator::Compile(
    kb::assets::AssetManager& assets,
    const VisualGraphCompileCoordinatorRequest& request,
    VisualGraphRuntimeRegistry& runtimeRegistry) {
    VisualGraphCompileCoordinatorResult result{};
    if (request.validateApiNames && (request.apiNames != nullptr || request.collectProjectApiNames)) {
        kb::script::ScriptApiNameRegistry mergedApiNames = request.apiNames == nullptr ? kb::script::ScriptApiNameRegistry{} : *request.apiNames;
        if (request.collectProjectApiNames) {
            const kb::script::ScriptApiNameCollectionResult collected = kb::script::ScriptApiNameCollector::CollectProjectAssets(assets);
            if (!collected.Succeeded()) {
                result.errors = collected.errors;
                result.diagnostics = collected.diagnostics;
                return result;
            }
            for (const kb::script::ScriptApiNameEntry& entry : collected.names.Entries()) {
                static_cast<void>(mergedApiNames.RegisterEntry(entry));
            }
        }
        const kb::script::ScriptApiNameValidationResult apiNames = mergedApiNames.Validate(request.disallowCrossKindApiNameCollisions);
        if (!apiNames.Succeeded()) {
            result.errors = apiNames.errors;
            result.diagnostics = apiNames.diagnostics;
            return result;
        }
    }

    if (!request.assetId.IsValid()) {
        result.errors.push_back("visual graph compile request has invalid asset id");
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::AssetLoad, result.errors.back()));
        return result;
    }

    const kb::assets::AssetHandle<VisualGraphAsset> graph = assets.Load<VisualGraphAsset>(request.assetId);
    if (!graph.IsLoaded()) {
        result.errors.push_back(assets.LastError().empty() ? "visual graph asset could not be loaded" : assets.LastError());
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::AssetLoad, result.errors.back()));
        return result;
    }

    VisualGraphBuildResult built = VisualGraphBuildPipeline::BuildNative(*graph.Get(), request.build);
    if (!built.Succeeded()) {
        SetErrors(result, std::move(built.errors), std::move(built.diagnostics), VisualGraphDiagnosticStage::Compile);
        return result;
    }

    result.artifact = VisualGraphRuntimeArtifact{
        .assetId = request.assetId,
        .graphName = graph->name,
        .module = std::move(built.module),
        .nativeCode = std::move(built.nativeCode),
    };

    const bool shouldWriteGeneratedCode = request.writeGeneratedCode || request.nativeBuild.enabled;
    if (shouldWriteGeneratedCode) {
        VisualGraphGeneratedCodeWriteResult written = VisualGraphGeneratedCodeWriter::Write(result.artifact.nativeCode, request.generatedCodeDirectory);
        if (!written.Succeeded()) {
            SetErrors(result, std::move(written.errors), {}, VisualGraphDiagnosticStage::GeneratedCodeWrite);
            return result;
        }
        result.artifact.generatedFiles = std::move(written.files);
    }

    if (request.nativeBuild.enabled) {
        VisualGraphNativeBuildResult builtNative = VisualGraphNativeBuildPipeline::Build(result.artifact.generatedFiles, request.nativeBuild);
        if (!builtNative.Succeeded()) {
            SetErrors(result, std::move(builtNative.errors), {}, VisualGraphDiagnosticStage::NativeBuild);
            return result;
        }
        result.nativeBuildSucceeded = true;
    }

    if (request.storeRuntimeArtifact) {
        runtimeRegistry.Store(result.artifact);
        result.runtimeArtifactStored = true;
    }
    return result;
}

} // namespace kb::visual
