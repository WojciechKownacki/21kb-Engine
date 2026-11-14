#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

// Lua scripts run on one of two VMs, chosen by the platform the game is built for (LuaJIT where the program may generate
// machine code, Lua 5.4 elsewhere), and a script must behave the same on both. They share a subset of the language:
// Lua 5.1 with the `bit` library, `unpack`, `goto`, numbers that are exact up to 2^53, and the standard libraries both have.
// A construct outside it works on one VM and fails, or means something else, on the other.
struct ScriptSubsetFinding {
    std::uint32_t line = 0U;
    // What is outside the subset and what to write instead.
    std::string message;
};

// Every construct of `luaSource` outside the shared subset, in source order: the operators `//` `&` `|` `~` `<<` `>>`,
// the `<const>` and `<close>` attributes, integer literals above 2^53, and the library functions only one VM has
// (math.type, string.pack, table.unpack, utf8, math.pow, ...). Strings and comments are not looked into.
[[nodiscard]] std::vector<ScriptSubsetFinding> FindOutOfSubsetConstructs(std::string_view luaSource);

} // namespace kb::script
