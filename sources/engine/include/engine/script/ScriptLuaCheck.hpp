#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

struct ScriptApiCatalog;

enum class ScriptLuaFindingSeverity : std::uint8_t {
    Warning, // loads and may run, but something in it will not do what it says
    Error,   // a call that fails when it runs
};

// One problem in a Lua script, with where it is. `kb_cli validate` and the language server (`kb_cli lsp`) both print
// exactly these, so a script that validates clean shows no squiggles and the other way round.
struct ScriptLuaFinding {
    ScriptLuaFindingSeverity severity = ScriptLuaFindingSeverity::Warning;
    std::uint32_t line = 1U;   // 1-based
    std::uint32_t column = 1U; // 1-based byte in the line
    std::uint32_t length = 1U; // bytes
    // What kind of problem: unknown-global, unknown-function, method-call, unknown-hook, foreign-lifecycle, bad-component,
    // bad-property, read-only-property, bad-argument-count, bad-argument-type, outside-subset.
    std::string code;
    std::string message;
};

// Checks a Lua behaviour or module against the script API `catalog`, without running it:
//  - a name called or indexed that is not defined anywhere (`Logg()`, `Foo.Bar()`), and a function of an engine group that
//    does not exist (`Physics.Raycat()`), with the closest real name; locals, parameters and the globals the script defines
//    are never flagged, and neither is a call the script guards (`World.GetName and World.GetName()`);
//  - a function named like an event hook (`OnTriggerEnte`) that the engine does not call, with the closest hook;
//  - a component or property named wrong in `GetProperty`/`SetProperty`/`HasComponent` (and `World.GetProperty*/SetProperty*`)
//    where the names are string literals, a write to a read-only property, and a literal of the wrong type;
//  - the wrong number of arguments, or a literal of the wrong type, in a call of a documented function;
//  - callbacks named for another engine (`Start`, `Update`) and constructs outside the Lua both script VMs share.
// Findings are in source order. A script that does not compile gets none from the first four groups beyond what can be read.
[[nodiscard]] std::vector<ScriptLuaFinding> CheckLuaScript(std::string_view source, const ScriptApiCatalog& catalog);

} // namespace kb::script
