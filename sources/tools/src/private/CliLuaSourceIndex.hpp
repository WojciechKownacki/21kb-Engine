#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {

// A place in a script: zero-based line and byte offset within the line.
struct LuaSpot {
    std::uint32_t line = 0U;
    std::uint32_t column = 0U;

    [[nodiscard]] bool operator<=(const LuaSpot& other) const noexcept {
        return line < other.line || (line == other.line && column <= other.column);
    }
};

struct LuaRange {
    LuaSpot begin;
    LuaSpot end;

    [[nodiscard]] bool Contains(LuaSpot spot) const noexcept { return begin <= spot && spot <= end; }
};

// A function or variable a script defines.
struct LuaDeclaration {
    std::string name;      // "Tick" for `function Tick`, `function Module.Tick`, `Module.Tick = function` and `local Tick = function`
    std::string qualifier; // "Module" for `function Module.Tick` and `function Module:Tick`, else empty
    bool isFunction = false;
    bool atTopLevel = false; // outside every block, table and call
    std::vector<std::string> parameters;
    LuaRange nameRange;
    LuaRange range; // `function` through its `end`, or the name of a variable
};

// `Import("name")` or `require("name")`; `alias` is the local the module is stored in (empty for a bare call).
struct LuaImport {
    std::string alias;
    std::string module;
    LuaRange moduleRange; // the string literal, quotes included
};

struct LuaSourceIndex {
    std::vector<LuaDeclaration> declarations;
    std::vector<LuaImport> imports;
};

// What a script defines and imports, read from its tokens (comments and strings are skipped). It does not check the
// script: a script that does not compile still gets the declarations that can be read.
[[nodiscard]] LuaSourceIndex IndexLuaSource(std::string_view source);

} // namespace kb::cli
