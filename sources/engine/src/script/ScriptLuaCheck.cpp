#include "engine/script/ScriptLuaCheck.hpp"

#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptEventHooks.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"
#include "engine/script/ScriptSandboxGlobals.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

enum class TokenKind : std::uint8_t { Name, Number, String, Symbol };

struct Token {
    TokenKind kind = TokenKind::Symbol;
    std::string text; // a string's contents without quotes
    std::uint32_t line = 1U;
    std::uint32_t column = 1U;
    std::uint32_t length = 1U;

    [[nodiscard]] bool Is(std::string_view symbol) const noexcept { return kind == TokenKind::Symbol && text == symbol; }
    [[nodiscard]] bool IsName(std::string_view name) const noexcept { return kind == TokenKind::Name && text == name; }
};

[[nodiscard]] bool IsNameStart(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || static_cast<unsigned char>(c) >= 0x80U;
}

[[nodiscard]] bool IsNameChar(char c) noexcept {
    return IsNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) != 0;
}

// The level of a long bracket opened at `at` ("[[" is 0, "[==[" is 2), or -1 when there is none.
[[nodiscard]] int LongBracketLevel(std::string_view source, std::size_t at) noexcept {
    if (at >= source.size() || source[at] != '[') return -1;
    std::size_t cursor = at + 1U;
    int level = 0;
    while (cursor < source.size() && source[cursor] == '=') {
        ++level;
        ++cursor;
    }
    return cursor < source.size() && source[cursor] == '[' ? level : -1;
}

[[nodiscard]] std::vector<Token> Tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::uint32_t line = 1U;
    std::size_t lineStart = 0U;
    const auto column = [&](std::size_t at) { return static_cast<std::uint32_t>(at - lineStart + 1U); };
    // Moves over `count` bytes that may include line ends.
    const auto advanceOver = [&](std::size_t from, std::size_t to) {
        for (std::size_t at = from; at < to && at < source.size(); ++at) {
            if (source[at] == '\n') {
                ++line;
                lineStart = at + 1U;
            }
        }
    };
    std::size_t at = 0U;
    while (at < source.size()) {
        const char c = source[at];
        if (c == '\n') {
            ++line;
            lineStart = ++at;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++at;
            continue;
        }
        if (source.compare(at, 2U, "--") == 0) {
            const int level = LongBracketLevel(source, at + 2U);
            if (level >= 0) {
                const std::string closing = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
                const std::size_t end = source.find(closing, at + 2U);
                const std::size_t stop = end == std::string_view::npos ? source.size() : end + closing.size();
                advanceOver(at, stop);
                at = stop;
            } else {
                const std::size_t end = source.find('\n', at);
                at = end == std::string_view::npos ? source.size() : end;
            }
            continue;
        }
        Token token;
        token.line = line;
        token.column = column(at);
        if (const int level = LongBracketLevel(source, at); level >= 0) {
            const std::size_t open = at + 2U + static_cast<std::size_t>(level);
            const std::string closing = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
            const std::size_t end = source.find(closing, open);
            const std::size_t stop = end == std::string_view::npos ? source.size() : end + closing.size();
            token.kind = TokenKind::String;
            token.text = std::string{ source.substr(open, (end == std::string_view::npos ? source.size() : end) - open) };
            const std::size_t firstLineEnd = source.find('\n', at);
            token.length = static_cast<std::uint32_t>(std::min(stop, firstLineEnd == std::string_view::npos ? stop : firstLineEnd) - at);
            tokens.push_back(std::move(token));
            advanceOver(at, stop);
            at = stop;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t cursor = at + 1U;
            std::string text;
            while (cursor < source.size() && source[cursor] != c && source[cursor] != '\n') {
                if (source[cursor] == '\\' && cursor + 1U < source.size()) {
                    ++cursor;
                    const char escaped = source[cursor];
                    text += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                    if (escaped == '\n') {
                        ++line;
                        lineStart = cursor + 1U;
                    }
                } else {
                    text += source[cursor];
                }
                ++cursor;
            }
            token.kind = TokenKind::String;
            token.text = std::move(text);
            token.length = static_cast<std::uint32_t>(std::min(cursor + 1U, source.size()) - at);
            tokens.push_back(std::move(token));
            at = cursor < source.size() && source[cursor] == c ? cursor + 1U : cursor;
            continue;
        }
        if (IsNameStart(c)) {
            std::size_t end = at;
            while (end < source.size() && IsNameChar(source[end])) ++end;
            token.kind = TokenKind::Name;
            token.text = std::string{ source.substr(at, end - at) };
            token.length = static_cast<std::uint32_t>(end - at);
            tokens.push_back(std::move(token));
            at = end;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && at + 1U < source.size() && std::isdigit(static_cast<unsigned char>(source[at + 1U])) != 0)) {
            std::size_t end = at;
            const bool hex = source.compare(at, 2U, "0x") == 0 || source.compare(at, 2U, "0X") == 0;
            while (end < source.size()) {
                const char d = source[end];
                if (std::isalnum(static_cast<unsigned char>(d)) != 0 || d == '_') {
                    ++end;
                } else if (d == '.' && source.compare(end, 2U, "..") != 0) {
                    ++end;
                } else if ((d == '+' || d == '-') && end > at && !hex && (source[end - 1U] == 'e' || source[end - 1U] == 'E')) {
                    ++end;
                } else {
                    break;
                }
            }
            token.kind = TokenKind::Number;
            token.text = std::string{ source.substr(at, end - at) };
            token.length = static_cast<std::uint32_t>(end - at);
            tokens.push_back(std::move(token));
            at = end;
            continue;
        }
        static constexpr std::array<std::string_view, 9> kLongSymbols{ "...", "==", "~=", "<=", ">=", "..", "::", "//", "<<" };
        std::size_t width = 1U;
        for (const std::string_view symbol : kLongSymbols) {
            if (source.compare(at, symbol.size(), symbol) == 0) {
                width = symbol.size();
                break;
            }
        }
        if (width == 1U && source.compare(at, 2U, ">>") == 0) width = 2U;
        token.text = std::string{ source.substr(at, width) };
        token.length = static_cast<std::uint32_t>(width);
        tokens.push_back(std::move(token));
        at += width;
    }
    return tokens;
}

[[nodiscard]] bool IsKeyword(std::string_view name) noexcept {
    static constexpr std::array<std::string_view, 22> kKeywords{ "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
        "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };
    return std::ranges::find(kKeywords, name) != kKeywords.end();
}

// The call of a function: where its parentheses close and the token range of every argument.
struct CallArguments {
    bool ok = false;
    std::size_t close = 0U;
    std::vector<std::pair<std::size_t, std::size_t>> arguments; // first and last token of each
};

enum class LiteralKind : std::uint8_t { Other, String, Number, Boolean, Nil, Table };

struct Literal {
    LiteralKind kind = LiteralKind::Other;
    double number = 0.0;
    std::string text;
};

struct MemberList {
    std::set<std::string, std::less<>> names;
};

using SortedNames = std::set<std::string, std::less<>>;

class Checker {
public:
    Checker(std::string_view source, const ScriptApiCatalog& catalog) : source_(source), catalog_(catalog), tokens_(Tokenize(source)) {}

    [[nodiscard]] std::vector<ScriptLuaFinding> Run() {
        BuildTables();
        CollectDeclarations();
        CheckUses();
        CheckHandlerNames();
        AddLifecycleAndSubsetFindings();
        std::ranges::stable_sort(findings_, [](const ScriptLuaFinding& a, const ScriptLuaFinding& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
        return std::move(findings_);
    }

private:
    // ---- what the engine defines ----------------------------------------------------------------------------------

    void BuildTables() {
        for (const ScriptApiCatalogLuaBinding& binding : catalog_.luaBindings) {
            if (binding.tableName.empty()) {
                globalFunctions_.insert(binding.luaName);
            } else {
                tables_[binding.tableName].names.insert(binding.luaName);
                bindings_[binding.tableName + "." + binding.luaName] = &binding;
            }
        }
        // Tables of the sandbox that are not function groups of the catalog.
        for (const std::string_view member : { "Subscribe", "Unsubscribe", "Emit", "EmitDeferred", "Broadcast" }) tables_["Events"].names.insert(std::string{ member });
        for (const ScriptApiCatalogComponent& component : catalog_.components) components_[component.name] = &component;
        for (const ScriptApiNameEntry& entry : catalog_.projectEntries) {
            if (entry.kind == ScriptApiNameKind::Event) projectEvents_.insert(entry.name);
        }
        for (const std::string& name : catalog_.declaredGlobals) declaredByProject_.insert(name);
    }

    [[nodiscard]] bool IsEngineName(std::string_view name) const {
        return ScriptSandboxGlobals::IsKnown(name) || tables_.contains(name) || globalFunctions_.contains(name) || declaredByProject_.contains(name);
    }

    // ---- reading the script ----------------------------------------------------------------------------------------

    [[nodiscard]] const Token& At(std::size_t index) const {
        static const Token kEnd{ TokenKind::Symbol, "", 0U, 0U, 0U };
        return index < tokens_.size() ? tokens_[index] : kEnd;
    }

    [[nodiscard]] bool IsMemberAccess(std::size_t index) const {
        return index > 0U && (At(index - 1U).Is(".") || At(index - 1U).Is(":"));
    }

    // Names introduced anywhere in the script: locals, parameters, loop variables, functions and assigned globals. Where
    // in the script they are introduced does not matter: a use before the declaration is not judged.
    void CollectDeclarations() {
        for (std::size_t i = 0U; i < tokens_.size(); ++i) {
            const Token& token = tokens_[i];
            if (token.kind == TokenKind::String) {
                stringLiterals_.insert(token.text);
                continue;
            }
            if (token.kind != TokenKind::Name) continue;
            if (token.text == "local") {
                std::size_t j = i + 1U;
                if (At(j).IsName("function")) {
                    if (At(j + 1U).kind == TokenKind::Name) declared_.insert(At(j + 1U).text);
                    continue;
                }
                while (At(j).kind == TokenKind::Name && !IsKeyword(At(j).text)) {
                    declared_.insert(At(j).text);
                    ++j;
                    if (At(j).Is("<") && At(j + 2U).Is(">")) j += 3U; // an attribute: <const>
                    if (At(j).Is(",")) ++j;
                    else break;
                }
            } else if (token.text == "function") {
                std::size_t j = i + 1U;
                if (At(j).kind == TokenKind::Name) {
                    const std::string& first = At(j).text;
                    declared_.insert(first);
                    std::size_t k = j + 1U;
                    bool method = false;
                    while ((At(k).Is(".") || At(k).Is(":")) && At(k + 1U).kind == TokenKind::Name) {
                        definedMembers_.insert(At(k + 1U).text);
                        method = At(k).Is(":");
                        k += 2U;
                    }
                    if (method) declared_.insert("self");
                    j = k;
                }
                if (At(j).Is("(")) {
                    for (std::size_t k = j + 1U; k < tokens_.size() && !At(k).Is(")"); ++k) {
                        if (At(k).kind == TokenKind::Name) declared_.insert(At(k).text);
                    }
                }
            } else if (token.text == "for") {
                for (std::size_t j = i + 1U; j < tokens_.size() && !At(j).IsName("in") && !At(j).Is("="); ++j) {
                    if (At(j).kind == TokenKind::Name) declared_.insert(At(j).text);
                    else if (!At(j).Is(",")) break;
                }
            } else if (!IsKeyword(token.text) && !IsMemberAccess(i)) {
                // An assignment `Name = ...`, `A, B = ...`, or a field `Name = ...` of a table.
                std::size_t j = i;
                std::vector<std::string> names;
                while (At(j).kind == TokenKind::Name && !IsKeyword(At(j).text)) {
                    names.push_back(At(j).text);
                    ++j;
                    if (At(j).Is(",")) ++j;
                    else break;
                }
                if (At(j).Is("=")) {
                    for (const std::string& name : names) declared_.insert(name);
                }
            }
            // A member that is assigned or defined as a function: `Counter.add = function`, `M.add = ...`; `_G.Name = ...`
            // defines the global Name.
            if (IsMemberAccess(i) && At(i + 1U).Is("=")) {
                definedMembers_.insert(token.text);
                if (i >= 2U && At(i - 1U).Is(".") && At(i - 2U).IsName("_G")) declared_.insert(token.text);
            }
        }
    }

    // ---- findings ---------------------------------------------------------------------------------------------------

    void Report(ScriptLuaFindingSeverity severity, const Token& at, std::uint32_t length, std::string code, std::string message) {
        findings_.push_back(ScriptLuaFinding{ severity, at.line, at.column, std::max(length, 1U), std::move(code), std::move(message) });
    }

    [[nodiscard]] CallArguments ParseArguments(std::size_t open) const {
        CallArguments result;
        int nest = 0;
        int block = 0;
        std::size_t start = open + 1U;
        if (At(start).Is(")")) {
            result.ok = true;
            result.close = start;
            return result;
        }
        for (std::size_t j = open + 1U; j < tokens_.size(); ++j) {
            const Token& token = tokens_[j];
            if (token.kind == TokenKind::Symbol) {
                if (token.text == "(" || token.text == "[" || token.text == "{") {
                    ++nest;
                } else if (token.text == ")" || token.text == "]" || token.text == "}") {
                    if (nest == 0 && token.text == ")" && block == 0) {
                        result.arguments.emplace_back(start, j - 1U);
                        result.ok = start < j;
                        result.close = j;
                        return result;
                    }
                    --nest;
                } else if (token.text == "," && nest == 0 && block == 0) {
                    if (start >= j) return result;
                    result.arguments.emplace_back(start, j - 1U);
                    start = j + 1U;
                }
            } else if (token.kind == TokenKind::Name) {
                if (token.text == "function" || token.text == "if" || token.text == "do" || token.text == "repeat") ++block;
                else if (token.text == "end" || token.text == "until") --block;
            }
        }
        return result;
    }

    [[nodiscard]] Literal Classify(std::pair<std::size_t, std::size_t> range) const {
        Literal literal;
        const std::size_t count = range.second - range.first + 1U;
        const Token& first = At(range.first);
        if (count == 1U) {
            if (first.kind == TokenKind::String) {
                literal.kind = LiteralKind::String;
                literal.text = first.text;
            } else if (first.kind == TokenKind::Number) {
                literal.kind = LiteralKind::Number;
                literal.number = std::strtod(first.text.c_str(), nullptr);
            } else if (first.IsName("true") || first.IsName("false")) {
                literal.kind = LiteralKind::Boolean;
            } else if (first.IsName("nil")) {
                literal.kind = LiteralKind::Nil;
            }
        } else if (count == 2U && first.Is("-") && At(range.first + 1U).kind == TokenKind::Number) {
            literal.kind = LiteralKind::Number;
            literal.number = -std::strtod(At(range.first + 1U).text.c_str(), nullptr);
        }
        if (first.Is("{")) literal.kind = LiteralKind::Table;
        return literal;
    }

    // Why a literal cannot be passed as a value of `type`; empty when it can or when it is not a literal. A property takes
    // exactly its own kind of value. The arguments of a function go through the Lua conversions: a number is accepted for a
    // string, a numeric string for a number, any value for a boolean, and a hash or asset id may be a number or a hex string,
    // so only a literal that fails the call is reported.
    [[nodiscard]] static std::string TypeProblem(const Literal& literal, ScriptValueType type, bool property) {
        if (literal.kind == LiteralKind::Other || literal.kind == LiteralKind::Nil || literal.kind == LiteralKind::Table || type == ScriptValueType::Void) return {};
        const bool numericText = literal.kind == LiteralKind::String && !literal.text.empty() && [&] {
            char* end = nullptr;
            std::strtod(literal.text.c_str(), &end);
            return end != nullptr && *end == 0;
        }();
        switch (type) {
        case ScriptValueType::Bool:
            return literal.kind == LiteralKind::Boolean || (!property && literal.kind != LiteralKind::String) ? std::string{} : std::string{ "a boolean (true or false)" };
        case ScriptValueType::String:
        case ScriptValueType::Name:
        case ScriptValueType::Guid:
            return literal.kind == LiteralKind::String || (!property && literal.kind == LiteralKind::Number) ? std::string{} : std::string{ "a string" };
        case ScriptValueType::Float:
        case ScriptValueType::Double:
            return literal.kind == LiteralKind::Number || (!property && numericText) ? std::string{} : std::string{ "a number" };
        case ScriptValueType::Hash:
            if (!property && literal.kind == LiteralKind::String) return {};
            [[fallthrough]];
        default: {
            if (literal.kind != LiteralKind::Number && !(!property && numericText)) return "a whole number";
            if (literal.kind == LiteralKind::Number && literal.number != std::floor(literal.number)) return "a whole number";
            return {};
        }
        }
    }

    [[nodiscard]] static const char* LiteralName(const Literal& literal) {
        return literal.kind == LiteralKind::String ? "a string" : literal.kind == LiteralKind::Boolean ? "a boolean" : "a number";
    }

    // ---- the use of names -------------------------------------------------------------------------------------------

    void CheckUses() {
        for (std::size_t i = 0U; i < tokens_.size(); ++i) {
            const Token& token = tokens_[i];
            if (token.kind != TokenKind::Name || IsKeyword(token.text)) continue;
            if (IsMemberAccess(i)) continue;
            const Token& before = At(i == 0U ? tokens_.size() : i - 1U);
            const Token& next = At(i + 1U);
            if (before.IsName("function") || before.IsName("goto") || before.Is("::") || next.Is("::")) continue;
            if (next.Is("=")) continue; // an assignment or a table field

            const bool used = next.Is("(") || next.Is(".") || next.Is(":");
            if (!used) {
                probed_.insert(token.text);
                continue;
            }
            if (token.text == "self" && next.Is(":")) {
                CheckSelfMethod(i);
                continue;
            }
            const bool shadowed = declared_.contains(token.text);
            if (!shadowed && !IsEngineName(token.text)) {
                if (!probed_.contains(token.text)) ReportUnknownGlobal(token);
                probed_.insert(token.text);
                continue;
            }
            probed_.insert(token.text);
            if (shadowed) continue;
            if (const auto table = tables_.find(token.text); table != tables_.end() && (next.Is(".") || next.Is(":"))) {
                CheckTableMember(i, table->second);
            }
        }
        // `self` is a parameter of every hook, but a function that forgot it has no such name.
        for (std::size_t i = 0U; i < tokens_.size(); ++i) {
            if (!tokens_[i].IsName("self") || IsMemberAccess(i) || declared_.contains("self")) continue;
            if (At(i + 1U).Is(".") || At(i + 1U).Is(":")) {
                Report(ScriptLuaFindingSeverity::Error, tokens_[i], 4U, "unknown-global",
                    "self is not defined here: a hook receives it as its first parameter, function Tick(self, dt)");
                break;
            }
        }
    }

    void ReportUnknownGlobal(const Token& token) {
        const std::string& name = token.text;
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 13> kUnavailable{ {
            { "require", "the sandbox has no require; share code with `-- @import Module.Name` and Import(\"Module.Name\") (kb_cli api --topic Import)" },
            { "os", "the sandbox has no os library" },
            { "io", "the sandbox has no io library" },
            { "debug", "the sandbox has no debug library" },
            { "package", "the sandbox has no package library" },
            { "module", "the sandbox has no module function" },
            { "load", "the sandbox cannot load code at run time" },
            { "loadstring", "the sandbox cannot load code at run time" },
            { "loadfile", "the sandbox cannot load files" },
            { "dofile", "the sandbox cannot load files" },
            { "collectgarbage", "the sandbox does not expose the garbage collector" },
            { "getfenv", "the sandbox does not expose environments" },
            { "setfenv", "the sandbox does not expose environments" },
        } };
        for (const auto& [unavailable, advice] : kUnavailable) {
            if (name == unavailable) {
                Report(ScriptLuaFindingSeverity::Error, token, token.length, "unknown-global", name + " is not available: " + std::string{ advice });
                return;
            }
        }
        std::vector<std::string> candidates;
        for (const ScriptSandboxGlobal& global : ScriptSandboxGlobals::All()) candidates.emplace_back(global.name);
        for (const auto& [table, members] : tables_) candidates.push_back(table);
        for (const std::string& function : globalFunctions_) candidates.push_back(function);
        for (const std::string& own : declared_) {
            if (own.size() > 2U) candidates.push_back(own);
        }
        const std::string closest = ClosestScriptName(name, std::span<const std::string>{ candidates });
        std::string message = name + " is not defined: it is not an engine function group, a global of the sandbox (kb_cli api --topic Globals) or something this script defines";
        if (!closest.empty()) message += "; did you mean " + closest + "?";
        Report(ScriptLuaFindingSeverity::Error, token, token.length, "unknown-global", std::move(message));
    }

    // `Table.Name(` or `Table:Name(` for an engine table.
    void CheckTableMember(std::size_t tableIndex, const MemberList& members) {
        const Token& table = tokens_[tableIndex];
        const Token& separator = At(tableIndex + 1U);
        const Token& member = At(tableIndex + 2U);
        if (member.kind != TokenKind::Name) return;
        const bool call = At(tableIndex + 3U).Is("(");
        const std::string full = table.text + "." + member.text;
        const std::uint32_t length = member.column + member.length - table.column;
        if (!call) {
            probedMembers_.insert(full);
            return;
        }
        if (separator.Is(":")) {
            Report(ScriptLuaFindingSeverity::Error, table, length, "method-call",
                table.text + ":" + member.text + "(...) passes the table as the first argument; write " + full + "(...) with a dot");
            return;
        }
        if (!members.names.contains(member.text)) {
            if (probedMembers_.contains(full) || definedMembers_.contains(member.text)) return;
            const std::vector<std::string> candidates{ members.names.begin(), members.names.end() };
            const std::string closest = ClosestScriptName(member.text, std::span<const std::string>{ candidates });
            std::string message = full + " does not exist";
            if (!closest.empty()) message += "; did you mean " + table.text + "." + closest + "?";
            message += " (kb_cli api --topic " + table.text + " lists the functions)";
            Report(ScriptLuaFindingSeverity::Error, table, length, "unknown-function", std::move(message));
            return;
        }
        const CallArguments arguments = ParseArguments(tableIndex + 3U);
        if (!arguments.ok) return;
        if (const auto binding = bindings_.find(full); binding != bindings_.end()) {
            if (const ScriptApiCatalogFunction* function = catalog_.FindFunction(binding->second->functionName)) {
                CheckCall(full, *function, arguments, table, length);
            }
        }
        if (table.text == "World" && IsWorldPropertyFunction(member.text)) {
            CheckWorldProperty(full, member.text.starts_with("SetProperty"), arguments);
        }
    }

    void CheckCall(const std::string& display, const ScriptApiCatalogFunction& function, const CallArguments& call, const Token& at, std::uint32_t length) {
        const std::size_t count = call.arguments.size();
        const std::size_t maximum = function.inputs.size();
        std::size_t minimum = 0U;
        for (std::size_t pin = 0U; pin < function.inputs.size(); ++pin) {
            if (function.inputs[pin].required) minimum = pin + 1U;
        }
        // One table is the inputs by name.
        if (count == 1U && At(call.arguments.front().first).Is("{")) return;
        const bool openEnded = count > 0U && (At(call.arguments.back().second).Is(")") || At(call.arguments.back().second).Is("..."));
        const bool maybeTable = count == 1U && Classify(call.arguments.front()).kind == LiteralKind::Other;
        std::string signature = display + "(";
        for (std::size_t pin = 0U; pin < function.inputs.size(); ++pin) {
            signature += (pin == 0U ? "" : ", ") + function.inputs[pin].name + (function.inputs[pin].required ? "" : "?");
        }
        signature += ")";
        if (count > maximum) {
            Report(ScriptLuaFindingSeverity::Error, at, length, "bad-argument-count",
                signature + " takes " + (maximum == 0U ? std::string{ "no arguments" } : std::to_string(maximum) + " argument" + (maximum == 1U ? "" : "s")) +
                    ", the call has " + std::to_string(count) + " (kb_cli api --topic " + display + ")");
            return;
        }
        if (count < minimum && !openEnded && !maybeTable) {
            Report(ScriptLuaFindingSeverity::Error, at, length, "bad-argument-count",
                signature + " needs " + std::to_string(minimum) + " argument" + (minimum == 1U ? "" : "s") + ", the call has " + std::to_string(count) +
                    " (kb_cli api --topic " + display + ")");
            return;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            const Literal literal = Classify(call.arguments[index]);
            const std::string wanted = TypeProblem(literal, function.inputs[index].type, false);
            if (!wanted.empty()) {
                const Token& argument = At(call.arguments[index].first);
                Report(ScriptLuaFindingSeverity::Error, argument, argument.length, "bad-argument-type",
                    display + " argument " + std::to_string(index + 1U) + " (" + function.inputs[index].name + ") must be " + wanted + ", not " + LiteralName(literal) +
                        " (" + signature + ")");
            }
        }
    }

    // ---- components and properties ----------------------------------------------------------------------------------

    void CheckSelfMethod(std::size_t selfIndex) {
        const Token& method = At(selfIndex + 2U);
        if (method.kind != TokenKind::Name || !At(selfIndex + 3U).Is("(")) return;
        static constexpr std::array<std::string_view, 5> kMethods{ "HasComponent", "GetProperty", "SetProperty", "GetVariable", "SetVariable" };
        if (std::ranges::find(kMethods, method.text) == kMethods.end()) {
            if (definedMembers_.contains(method.text)) return;
            const std::string closest = ClosestScriptName(method.text, std::span<const std::string_view>{ kMethods });
            std::string message = "self:" + method.text + "() does not exist";
            if (!closest.empty()) message += "; did you mean self:" + closest + "?";
            message += " (self has HasComponent, GetProperty, SetProperty, GetVariable, SetVariable)";
            Report(ScriptLuaFindingSeverity::Error, tokens_[selfIndex], method.column + method.length - tokens_[selfIndex].column, "unknown-function", std::move(message));
            return;
        }
        if (definedMembers_.contains(method.text)) return;
        const CallArguments call = ParseArguments(selfIndex + 3U);
        if (!call.ok) return;
        const std::string display = "self:" + method.text;
        if (method.text == "HasComponent") {
            if (call.arguments.size() >= 1U) CheckComponentName(display, call.arguments[0]);
        } else if (method.text == "GetProperty" || method.text == "SetProperty") {
            const bool write = method.text == "SetProperty";
            const std::size_t expected = write ? 3U : 2U;
            const std::size_t given = call.arguments.size();
            const bool openEnded = given > 0U && (At(call.arguments.back().second).Is(")") || At(call.arguments.back().second).Is("..."));
            if (given > expected || (given < expected && !openEnded)) {
                Report(ScriptLuaFindingSeverity::Error, tokens_[selfIndex], method.column + method.length - tokens_[selfIndex].column, "bad-argument-count",
                    display + (write ? "(component, property, value)" : "(component, property)") + " takes " + std::to_string(expected) + " arguments, the call has " +
                        std::to_string(call.arguments.size()));
                return;
            }
            CheckPropertyNames(display, call, 0U, 1U, write ? 2U : std::string::npos);
        }
    }

    [[nodiscard]] static bool IsWorldPropertyFunction(std::string_view name) {
        for (const std::string_view prefix : { "GetProperty", "SetProperty" }) {
            if (!name.starts_with(prefix)) continue;
            const std::string_view type = name.substr(prefix.size());
            return type == "Bool" || type == "Int" || type == "UInt32" || type == "Float" || type == "String" || type == "Entity" || type == "Hash";
        }
        return false;
    }

    void CheckWorldProperty(const std::string& display, bool write, const CallArguments& call) {
        if (call.arguments.size() >= 3U) CheckPropertyNames(display, call, 1U, 2U, write && call.arguments.size() >= 4U ? 3U : std::string::npos);
    }

    [[nodiscard]] const ScriptApiCatalogComponent* FindComponent(std::string_view literalName) const {
        const std::string canonical{ kb::scene::SceneComponentAuthoring::CanonicalName(literalName) };
        const auto found = components_.find(canonical);
        return found == components_.end() ? nullptr : found->second;
    }

    // Reports an unknown component; true when `name` is not known.
    bool CheckComponentName(const std::string& display, std::pair<std::size_t, std::size_t> range) {
        const Literal literal = Classify(range);
        if (literal.kind != LiteralKind::String || FindComponent(literal.text) != nullptr) return false;
        std::vector<std::string> names;
        for (const auto& [name, component] : components_) names.push_back(name);
        const std::string closest = ClosestScriptName(literal.text, std::span<const std::string>{ names });
        const Token& at = At(range.first);
        std::string message = display + ": there is no component called \"" + literal.text + "\"";
        if (!closest.empty()) message += "; did you mean \"" + closest + "\"?";
        message += " (kb_cli api --topic index lists the components; kb_cli schema --component <Name> lists the properties)";
        Report(ScriptLuaFindingSeverity::Error, at, at.length, "bad-component", std::move(message));
        return true;
    }

    void CheckPropertyNames(const std::string& display, const CallArguments& call, std::size_t componentIndex, std::size_t propertyIndex, std::size_t valueIndex) {
        if (componentIndex >= call.arguments.size() || propertyIndex >= call.arguments.size()) return;
        if (CheckComponentName(display, call.arguments[componentIndex])) return;
        const Literal component = Classify(call.arguments[componentIndex]);
        const Literal property = Classify(call.arguments[propertyIndex]);
        if (component.kind != LiteralKind::String || property.kind != LiteralKind::String) return;
        const ScriptApiCatalogComponent* info = FindComponent(component.text);
        if (info == nullptr) return;
        const ScriptApiCatalogProperty* found = nullptr;
        std::vector<std::string> names;
        for (const ScriptApiCatalogProperty& candidate : info->properties) {
            names.push_back(candidate.name);
            if (candidate.name == property.text) found = &candidate;
        }
        const Token& at = At(call.arguments[propertyIndex].first);
        if (found == nullptr) {
            const std::string closest = ClosestScriptName(property.text, std::span<const std::string>{ names });
            std::string message = display + ": " + info->name + " has no property \"" + property.text + "\"";
            if (!closest.empty()) message += "; did you mean \"" + closest + "\"?";
            message += " (kb_cli schema --component " + info->name + " lists them)";
            Report(ScriptLuaFindingSeverity::Error, at, at.length, "bad-property", std::move(message));
            return;
        }
        if (valueIndex == std::string::npos || valueIndex >= call.arguments.size()) return;
        if (!found->writable) {
            Report(ScriptLuaFindingSeverity::Error, at, at.length, "read-only-property", display + ": " + info->name + "." + found->name + " is read-only");
            return;
        }
        const Literal value = Classify(call.arguments[valueIndex]);
        const std::string wanted = TypeProblem(value, found->type, true);
        if (!wanted.empty()) {
            const Token& valueToken = At(call.arguments[valueIndex].first);
            Report(ScriptLuaFindingSeverity::Error, valueToken, valueToken.length, "bad-argument-type",
                display + ": " + info->name + "." + found->name + " is a " + std::string{ ToString(found->type) } + ", the value must be " + wanted + ", not " + LiteralName(value));
        }
    }

    // ---- event hooks ------------------------------------------------------------------------------------------------

    void CheckHandlerNames() {
        std::set<std::string> foreign;
        for (const ForeignLifecycleName& name : FindForeignLifecycleNames(source_)) foreign.insert(name.name);
        for (std::size_t i = 0U; i + 2U < tokens_.size(); ++i) {
            if (!tokens_[i].IsName("function") || tokens_[i + 1U].kind != TokenKind::Name || !tokens_[i + 2U].Is("(")) continue;
            if (i > 0U && tokens_[i - 1U].IsName("local")) continue;
            const Token& name = tokens_[i + 1U];
            const std::string& text = name.text;
            if (text.size() < 3U || !text.starts_with("On") || std::isupper(static_cast<unsigned char>(text[2])) == 0) continue;
            if (ScriptEventHooks::Find(text) != nullptr || foreign.contains(text) || stringLiterals_.contains(text) || projectEvents_.contains(text)) continue;
            const std::string_view closest = ScriptEventHooks::Closest(text);
            std::string message = text + "() is not an event hook of the engine, so it runs only when a script emits an event called " + text + " (Emit)";
            if (!closest.empty()) message += "; if you meant the hook, it is " + std::string{ closest } + "(self, event)";
            message += ". All hooks: kb_cli api --topic EventHooks";
            Report(ScriptLuaFindingSeverity::Warning, name, name.length, "unknown-hook", std::move(message));
        }
    }

    void AddLifecycleAndSubsetFindings() {
        std::vector<std::string_view> lines;
        for (std::size_t begin = 0U;;) {
            const std::size_t end = source_.find('\n', begin);
            lines.push_back(source_.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
            if (end == std::string_view::npos) break;
            begin = end + 1U;
        }
        for (const ForeignLifecycleName& name : FindForeignLifecycleNames(source_)) {
            const char* hook = ToString(name.hook);
            std::uint32_t column = 1U;
            if (name.line >= 1U && name.line <= lines.size()) {
                const std::size_t found = lines[name.line - 1U].find(name.name);
                if (found != std::string_view::npos) column = static_cast<std::uint32_t>(found + 1U);
            }
            findings_.push_back(ScriptLuaFinding{ ScriptLuaFindingSeverity::Warning, name.line, column, static_cast<std::uint32_t>(name.name.size()), "foreign-lifecycle",
                name.name + "() is never called - 21kb calls " + hook + "(self, dt). Rename the function to " + hook + "." });
        }
        for (const ScriptSubsetFinding& finding : FindOutOfSubsetConstructs(source_)) {
            findings_.push_back(ScriptLuaFinding{ ScriptLuaFindingSeverity::Warning, finding.line, finding.column, finding.length, "outside-subset", finding.message });
        }
    }

    std::string_view source_;
    const ScriptApiCatalog& catalog_;
    std::vector<Token> tokens_;
    std::vector<ScriptLuaFinding> findings_;
    std::map<std::string, MemberList, std::less<>> tables_;
    std::map<std::string, const ScriptApiCatalogLuaBinding*, std::less<>> bindings_;
    std::map<std::string, const ScriptApiCatalogComponent*, std::less<>> components_;
    SortedNames globalFunctions_;
    SortedNames projectEvents_;
    SortedNames declaredByProject_;
    SortedNames declared_;
    SortedNames definedMembers_;
    SortedNames stringLiterals_;
    SortedNames probed_;
    SortedNames probedMembers_;
};

} // namespace

std::vector<ScriptLuaFinding> CheckLuaScript(std::string_view source, const ScriptApiCatalog& catalog) {
    return Checker{ source, catalog }.Run();
}

} // namespace kb::script
