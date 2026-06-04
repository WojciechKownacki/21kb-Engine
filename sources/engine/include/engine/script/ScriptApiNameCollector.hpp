#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"

#include <string>
#include <vector>

namespace kb::script {

struct ScriptApiNameCollectionResult {
    ScriptApiNameRegistry names;
    std::vector<std::string> errors;
    std::vector<kb::visual::VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !kb::visual::VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class ScriptApiNameCollector final {
public:
    ScriptApiNameCollector() = delete;

    [[nodiscard]] static ScriptApiNameCollectionResult CollectProjectAssets(kb::assets::AssetManager& assets);
};

} // namespace kb::script
