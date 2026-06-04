#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/visual/VisualGraphBuildPipeline.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphCompileCoordinatorRequest {
    kb::assets::AssetId assetId{};
    VisualGraphBuildDesc build;
    std::filesystem::path generatedCodeDirectory;
    const kb::script::ScriptApiNameRegistry* apiNames = nullptr;
    bool validateApiNames = true;
    bool collectProjectApiNames = true;
    bool disallowCrossKindApiNameCollisions = false;
    bool writeGeneratedCode = false;
    VisualGraphNativeBuildDesc nativeBuild;
    bool storeRuntimeArtifact = true;
};

struct VisualGraphCompileCoordinatorResult {
    VisualGraphRuntimeArtifact artifact;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;
    bool runtimeArtifactStored = false;
    bool nativeBuildSucceeded = false;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphCompileCoordinator final {
public:
    VisualGraphCompileCoordinator() = delete;

    [[nodiscard]] static VisualGraphCompileCoordinatorResult Compile(
        kb::assets::AssetManager& assets,
        const VisualGraphCompileCoordinatorRequest& request,
        VisualGraphRuntimeRegistry& runtimeRegistry);
};

} // namespace kb::visual
