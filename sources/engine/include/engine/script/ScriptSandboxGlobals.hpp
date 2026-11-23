#pragma once

#include <span>
#include <string>
#include <string_view>

namespace kb::script {

// A name a Lua script can use without defining it, besides the engine's function groups (Physics, Transform, ...):
// the functions and tables the sandbox adds (Log, Emit, EmitTo, Import, SetShared, GetShared, HasShared, RemoveShared,
// CallFunction, Events, Inspector) and the standard Lua names it keeps (assert, pcall, math, string, ...). Each has its
// own page in `kb_cli api --topic <Name>`, and `kb_cli validate` treats them as defined.
struct ScriptSandboxGlobal {
    std::string_view name;
    std::string_view kind;      // "function", "table" or "library"
    std::string_view signature; // `Log(value)`, `Events.Subscribe(name, handler)`, ...
    std::string_view summary;   // one line, what it does
    std::string_view details;   // markdown: behaviour, return values, an example
};

class ScriptSandboxGlobals final {
public:
    ScriptSandboxGlobals() = delete;

    [[nodiscard]] static std::span<const ScriptSandboxGlobal> All() noexcept;
    // The global called exactly `name` (letter case counts), or null.
    [[nodiscard]] static const ScriptSandboxGlobal* Find(std::string_view name) noexcept;
    // Whether a script may use `name` as a global: one of the entries above or a name a script of the sandbox always has
    // (_G, and the Lua 5.4 names rawlen and utf8 that `kb_cli validate` warns about separately).
    [[nodiscard]] static bool IsKnown(std::string_view name) noexcept;
    // The page of one global.
    [[nodiscard]] static std::string Page(const ScriptSandboxGlobal& global);
    // The list of all of them, one line each, with the topic to ask for.
    [[nodiscard]] static std::string IndexPage();
};

} // namespace kb::script
