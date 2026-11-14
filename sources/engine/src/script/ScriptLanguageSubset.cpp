#include "engine/script/ScriptLanguageSubset.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace kb::script {
namespace {

struct LibraryName {
    std::string_view library;
    std::string_view name;
    std::string_view advice;
};

// Functions one VM has and the other lacks (or has with another meaning).
constexpr LibraryName kLibraryNames[] = {
    { "math", "type", "there is no integer type on LuaJIT; every number is a double" },
    { "math", "tointeger", "there is no integer type on LuaJIT; use math.floor" },
    { "math", "maxinteger", "integers are exact up to 2^53 on every platform" },
    { "math", "mininteger", "integers are exact up to 2^53 on every platform" },
    { "math", "ult", "there are no 64-bit integers on LuaJIT" },
    { "math", "pow", "only LuaJIT has it; write a ^ b" },
    { "math", "log10", "only LuaJIT has it; write math.log(x) / math.log(10)" },
    { "math", "cosh", "only LuaJIT has it" },
    { "math", "sinh", "only LuaJIT has it" },
    { "math", "tanh", "only LuaJIT has it" },
    { "math", "ldexp", "only LuaJIT has it; multiply by 2 ^ e" },
    { "math", "frexp", "only LuaJIT has it" },
    { "math", "mod", "only LuaJIT has it; write a % b" },
    { "string", "pack", "only Lua 5.4 has it" },
    { "string", "unpack", "only Lua 5.4 has it" },
    { "string", "packsize", "only Lua 5.4 has it" },
    { "string", "gfind", "only LuaJIT has it; use string.gmatch" },
    { "table", "pack", "only Lua 5.4 has it; write { ... } and select('#', ...)" },
    { "table", "move", "only Lua 5.4 has it" },
    { "table", "unpack", "LuaJIT has no table.unpack; use unpack, which both VMs have" },
    { "table", "maxn", "only LuaJIT has it" },
    { "table", "getn", "only LuaJIT has it; use #" },
    { "table", "foreach", "only LuaJIT has it; use pairs" },
    { "table", "foreachi", "only LuaJIT has it; use ipairs" },
    { "coroutine", "close", "only Lua 5.4 has it" },
    { "coroutine", "isyieldable", "only Lua 5.4 has it" },
};

constexpr std::string_view kForeignGlobals[] = { "_ENV", "rawlen", "loadstring", "setfenv", "getfenv", "newproxy" };

[[nodiscard]] bool IsNameStart(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool IsNameChar(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The number of '=' of a long bracket opened at `position` ("[[" is zero, "[==[" is two), or -1 when it is no long bracket.
[[nodiscard]] int LongBracketLevel(std::string_view source, std::size_t position) noexcept {
    if (position >= source.size() || source[position] != '[') return -1;
    std::size_t end = position + 1U;
    while (end < source.size() && source[end] == '=') ++end;
    return end < source.size() && source[end] == '[' ? static_cast<int>(end - position - 1U) : -1;
}

} // namespace

std::vector<ScriptSubsetFinding> FindOutOfSubsetConstructs(std::string_view source) {
    std::vector<ScriptSubsetFinding> findings;
    std::uint32_t line = 1U;
    const auto report = [&](std::string message) {
        findings.push_back(ScriptSubsetFinding{ .line = line, .message = std::move(message) });
    };
    // Skips a long bracket that opens at `position` (after counting its lines); returns the position after it.
    const auto skipLong = [&](std::size_t position, int level) {
        const std::string closing = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
        const std::size_t end = source.find(closing, position);
        const std::size_t stop = end == std::string_view::npos ? source.size() : end + closing.size();
        line += static_cast<std::uint32_t>(std::count(source.begin() + static_cast<std::ptrdiff_t>(position), source.begin() + static_cast<std::ptrdiff_t>(stop), '\n'));
        return stop;
    };
    // The name after `.` at `position` (skipping spaces), or empty.
    const auto nameAfterDot = [&](std::size_t position) -> std::string_view {
        while (position < source.size() && (source[position] == ' ' || source[position] == '\t')) ++position;
        if (position >= source.size() || source[position] != '.' || (position + 1U < source.size() && source[position + 1U] == '.')) return {};
        ++position;
        while (position < source.size() && (source[position] == ' ' || source[position] == '\t')) ++position;
        std::size_t end = position;
        while (end < source.size() && IsNameChar(source[end])) ++end;
        return source.substr(position, end - position);
    };

    std::size_t position = 0U;
    bool afterDotOrColon = false; // the previous token was '.' or ':' (so a name is a field, not a global)
    while (position < source.size()) {
        const char c = source[position];
        if (c == '\n') {
            ++line;
            ++position;
            continue;
        }
        if (c == '-' && position + 1U < source.size() && source[position + 1U] == '-') { // a comment
            const int level = LongBracketLevel(source, position + 2U);
            if (level >= 0) {
                position = skipLong(position + 2U, level);
            } else {
                while (position < source.size() && source[position] != '\n') ++position;
            }
            continue;
        }
        if (c == '"' || c == '\'') { // a string
            ++position;
            while (position < source.size() && source[position] != c && source[position] != '\n') {
                if (source[position] == '\\' && position + 1U < source.size() && source[position + 1U] == '\n') ++line; // a line continued by a backslash
                position += source[position] == '\\' ? 2U : 1U;
            }
            if (position < source.size() && source[position] == c) ++position;
            continue;
        }
        if (c == '[') {
            const int level = LongBracketLevel(source, position);
            if (level >= 0) {
                position = skipLong(position, level);
                afterDotOrColon = false;
                continue;
            }
        }
        if (IsNameStart(c)) {
            std::size_t end = position;
            while (end < source.size() && IsNameChar(source[end])) ++end;
            const std::string_view name = source.substr(position, end - position);
            if (!afterDotOrColon) {
                if (std::ranges::find(kForeignGlobals, name) != std::end(kForeignGlobals)) {
                    report(std::string{ name } + " exists on one VM only; scripts must run on both");
                } else if (name == "utf8") {
                    report("the utf8 library exists on Lua 5.4 only; scripts must run on both");
                } else if (const std::string_view member = nameAfterDot(end); !member.empty()) {
                    for (const LibraryName& entry : kLibraryNames) {
                        if (entry.library == name && entry.name == member) {
                            report(std::string{ name } + "." + std::string{ member } + ": " + std::string{ entry.advice });
                        }
                    }
                }
            }
            position = end;
            afterDotOrColon = false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && position + 1U < source.size() && std::isdigit(static_cast<unsigned char>(source[position + 1U])) != 0)) {
            std::size_t end = position;
            const bool hex = c == '0' && position + 1U < source.size() && (source[position + 1U] == 'x' || source[position + 1U] == 'X');
            bool plainInteger = true;
            while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) != 0 || source[end] == '.' ||
                   ((source[end] == '+' || source[end] == '-') && end > position && !hex && (source[end - 1U] == 'e' || source[end - 1U] == 'E')))) {
                if (source[end] == '.' || (!hex && (source[end] == 'e' || source[end] == 'E'))) plainInteger = false;
                ++end;
            }
            if (plainInteger) {
                const std::string text{ source.substr(hex ? position + 2U : position, end - position - (hex ? 2U : 0U)) };
                const std::size_t significant = text.find_first_not_of('0');
                const std::size_t digits = significant == std::string::npos ? 0U : text.size() - significant;
                const bool tooLarge = hex ? digits > 14U || (digits == 14U && text[significant] > '1')
                                          : digits > 16U || (digits == 16U && text.compare(significant, 16U, "9007199254740992") > 0);
                if (tooLarge) report("an integer literal above 2^53 is not exact on every platform");
            }
            position = end;
            afterDotOrColon = false;
            continue;
        }
        const char next = position + 1U < source.size() ? source[position + 1U] : '\0';
        if (c == '/' && next == '/') {
            report("'//' (integer division) exists on Lua 5.4 only; write math.floor(a / b)");
            position += 2U;
        } else if (c == '<' && next == '<') {
            report("'<<' exists on Lua 5.4 only; use the bit library: bit.lshift(a, n)");
            position += 2U;
        } else if (c == '>' && next == '>') {
            report("'>>' exists on Lua 5.4 only; use the bit library: bit.rshift(a, n)");
            position += 2U;
        } else if (c == '&') {
            report("'&' exists on Lua 5.4 only; use the bit library: bit.band(a, b)");
            ++position;
        } else if (c == '|') {
            report("'|' exists on Lua 5.4 only; use the bit library: bit.bor(a, b)");
            ++position;
        } else if (c == '~' && next != '=') {
            report("'~' exists on Lua 5.4 only; use the bit library: bit.bxor(a, b) or bit.bnot(a)");
            ++position;
        } else if (c == '<') { // <const> and <close>
            std::size_t cursor = position + 1U;
            while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t')) ++cursor;
            std::size_t end = cursor;
            while (end < source.size() && IsNameChar(source[end])) ++end;
            const std::string_view attribute = source.substr(cursor, end - cursor);
            while (end < source.size() && (source[end] == ' ' || source[end] == '\t')) ++end;
            if ((attribute == "const" || attribute == "close") && end < source.size() && source[end] == '>') {
                report("'<" + std::string{ attribute } + ">' exists on Lua 5.4 only; leave the attribute out");
                position = end + 1U;
            } else {
                ++position;
            }
        } else if (c == '.' && next == '.') { // concatenation or the vararg
            position += source.compare(position, 3U, "...") == 0 ? 3U : 2U;
            afterDotOrColon = false;
            continue;
        } else {
            ++position;
        }
        if (c != ' ' && c != '\t' && c != '\r') afterDotOrColon = c == '.' || c == ':';
    }
    return findings;
}

} // namespace kb::script
