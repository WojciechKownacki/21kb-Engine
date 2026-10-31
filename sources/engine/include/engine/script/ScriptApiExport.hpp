#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

// Renders a ScriptApiCatalog into agent- and tooling-facing artifacts:
// - Markdown reference for humans and LLM coding agents,
// - a Lua Language Server definition file (---@meta stubs) so editors and
//   agents get completion plus diagnostics for the script sandbox,
// - machine-readable JSON for external tools (MCP clients, editors),
// - the same reference split into small pages, so an agent reads only the
//   function or component it is about to use instead of the whole surface.
struct ScriptApiReferencePage {
    std::string path;     // relative, forward slashes: "README.md", "functions/Physics/Raycast.md"
    std::string content;
};

class ScriptApiExport final {
public:
    ScriptApiExport() = delete;

    [[nodiscard]] static std::string ToMarkdown(const ScriptApiCatalog& catalog);
    // README.md (an index of groups and components), functions/<Group>.md (that group's
    // functions in one table), functions/<Group>/<Function>.md (one function: pins, Lua
    // call, example) and components/<Component>.md (its properties).
    [[nodiscard]] static std::vector<ScriptApiReferencePage> ToReferencePages(const ScriptApiCatalog& catalog);
    // One page by topic: empty or "index" for the index, a group ("Physics"), a function
    // ("Physics.Raycast") or a component ("Rigidbody"), matched without regard to case.
    // Empty when nothing is called that.
    [[nodiscard]] static std::optional<std::string> ToReferenceTopic(const ScriptApiCatalog& catalog, std::string_view topic);
    // The functions and components whose name or description mention every word of `text`,
    // one line each with the topic to ask for, at most `limit` of them.
    [[nodiscard]] static std::string SearchReference(const ScriptApiCatalog& catalog, std::string_view text, std::size_t limit = 25U);
    [[nodiscard]] static std::string ToLuaStubs(const ScriptApiCatalog& catalog);
    [[nodiscard]] static std::string ToJson(const ScriptApiCatalog& catalog);
};

} // namespace kb::script
