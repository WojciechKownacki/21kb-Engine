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
    // LIB-013: true if this call wrote a file under projectRoot/Assets/
    // (currently starter scripts, projectile prefabs, gameplay samples, and
    // the Audio Shooter demo scene/assets on a project's
    // first run — it is write-once). Such a file becomes a newly
    // discoverable project asset, which the catalog THIS call already
    // wrote kb.lua/script_api.md/script_api.json/manifest.json from does
    // NOT yet reflect (that catalog was necessarily built before this
    // call). A caller that cares about those generated files staying
    // consistent with the project's actual final asset set (e.g. so a
    // manifest hash does not drift on the very next, otherwise-identical
    // run) should rebuild its catalog and call Write() again when this is
    // true — the second call's own wroteProjectAsset will be false, since
    // the write-once file now already exists.
    bool wroteProjectAsset = false;
};

// Provisions a game project directory for AI coding agents and editors:
// AGENTS.md working instructions, a .luarc.json for
// the Lua Language Server, generated API artifacts under .kb/api/, and a
// minimal Assets/Logic/PlayerController.lua starter behaviour (LIB-013) —
// the exact script AGENTS.md's own worked example references. AGENTS.md,
// .luarc.json, PlayerController.lua, and demo assets are only created when missing
// (users may customize them); everything under .kb/api/ is always
// regenerated.
class ScriptAgentProjectFiles final {
public:
    ScriptAgentProjectFiles() = delete;

    [[nodiscard]] static ScriptAgentProjectFilesResult Write(
        const std::filesystem::path& projectRoot,
        const ScriptApiCatalog& catalog);
};

} // namespace kb::script
