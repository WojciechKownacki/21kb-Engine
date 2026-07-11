#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::script {

struct ScriptAgentProjectFilesResult {
    bool succeeded = false;
    std::vector<std::filesystem::path> writtenFiles;
    std::vector<std::filesystem::path> skippedFiles;
    std::string error;
};

// Provisions a game project directory for AI coding agents (VS Code + Claude
// Code / Copilot / Cursor): AGENTS.md working instructions, a .luarc.json for
// the Lua Language Server, and generated API artifacts under .kb/api/.
// AGENTS.md and .luarc.json are only created when missing (users may customize
// them); everything under .kb/api/ is always regenerated.
class ScriptAgentProjectFiles final {
public:
    ScriptAgentProjectFiles() = delete;

    [[nodiscard]] static ScriptAgentProjectFilesResult Write(
        const std::filesystem::path& projectRoot,
        const ScriptApiCatalog& catalog);
};

} // namespace kb::script
