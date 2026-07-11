#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

#include <string>

namespace kb::script {

// Renders a ScriptApiCatalog into agent- and tooling-facing artifacts:
// - Markdown reference for humans and LLM coding agents,
// - a Lua Language Server definition file (---@meta stubs) so editors and
//   agents get completion plus diagnostics for the script sandbox,
// - machine-readable JSON for external tools (MCP clients, editors).
class ScriptApiExport final {
public:
    ScriptApiExport() = delete;

    [[nodiscard]] static std::string ToMarkdown(const ScriptApiCatalog& catalog);
    [[nodiscard]] static std::string ToLuaStubs(const ScriptApiCatalog& catalog);
    [[nodiscard]] static std::string ToJson(const ScriptApiCatalog& catalog);
};

} // namespace kb::script
