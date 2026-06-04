#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/visual/VisualGraphBuildPipeline.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphCompileRequest {
    VisualGraphBuildDesc build;
    std::filesystem::path generatedCodeDirectory;
    bool writeGeneratedCode = true;
};

struct VisualGraphCompileServiceResult {
    VisualGraphRuntimeArtifact artifact;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphCompileService {
public:
    VisualGraphCompileService() = delete;

    [[nodiscard]] static VisualGraphCompileServiceResult CompileAsset(
        kb::assets::AssetManager& assets,
        kb::assets::AssetId assetId,
        const VisualGraphCompileRequest& request,
        VisualGraphRuntimeRegistry& runtimeRegistry);
};

} // namespace kb::visual
