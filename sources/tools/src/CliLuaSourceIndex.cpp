#include "CliLuaSourceIndex.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace kb::cli {
namespace {

enum class TokenKind { Name, String, Number, Symbol };

struct Token {
    TokenKind kind = TokenKind::Symbol;
    std::string_view text; // a quoted string's text, without its quotes
    LuaRange range;
};

[[nodiscard]] bool IsNameStart(char c) noexcept { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
[[nodiscard]] bool IsNameChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// The number of '=' of a long bracket opened at `position` ("[[" is zero), or -1 when none opens there.
[[nodiscard]] int LongBracketLevel(std::string_view source, std::size_t position) noexcept {
    if (position >= source.size() || source[position] != '[') return -1;
    std::size_t end = position + 1U;
    while (end < source.size() && source[end] == '=') ++end;
    return end < source.size() && source[end] == '[' ? static_cast<int>(end - position - 1U) : -1;
}

// The position after the bracket that closes the long bracket of `level` opened at `position`.
[[nodiscard]] std::size_t LongBracketEnd(std::string_view source, std::size_t position, int level) {
    const std::string closing = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
    const std::size_t end = source.find(closing, position);
    return end == std::string_view::npos ? source.size() : end + closing.size();
}

[[nodiscard]] std::vector<Token> Tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::uint32_t line = 0U;
    std::size_t lineStart = 0U;
    std::size_t at = 0U;
    const auto here = [&] { return LuaSpot{ line, static_cast<std::uint32_t>(at - lineStart) }; };
    const auto advance = [&](std::size_t to) {
        for (to = std::min(to, source.size()); at < to; ++at) {
            if (source[at] == '\n') {
                ++line;
                lineStart = at + 1U;
            }
        }
    };
    while (at < source.size()) {
        const char c = source[at];
        const char next = at + 1U < source.size() ? source[at + 1U] : '\0';
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            advance(at + 1U);
            continue;
        }
        if (c == '-' && next == '-') {
            if (const int level = LongBracketLevel(source, at + 2U); level >= 0) {
                advance(LongBracketEnd(source, at + 2U, level));
            } else {
                const std::size_t end = source.find('\n', at);
                advance(end == std::string_view::npos ? source.size() : end);
            }
            continue;
        }
        Token token;
        token.range.begin = here();
        if (c == '"' || c == '\'') {
            std::size_t end = at + 1U;
            while (end < source.size() && source[end] != c && source[end] != '\n') end += source[end] == '\\' ? 2U : 1U;
            end = std::min(end, source.size());
            token.kind = TokenKind::String;
            token.text = source.substr(at + 1U, end - at - 1U);
            advance(end < source.size() && source[end] == c ? end + 1U : end);
        } else if (const int level = LongBracketLevel(source, at); level >= 0) {
            const std::size_t end = LongBracketEnd(source, at, level);
            token.kind = TokenKind::String;
            token.text = source.substr(at, end - at);
            advance(end);
        } else if (IsNameStart(c)) {
            std::size_t end = at;
            while (end < source.size() && IsNameChar(source[end])) ++end;
            token.kind = TokenKind::Name;
            token.text = source.substr(at, end - at);
            advance(end);
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && std::isdigit(static_cast<unsigned char>(next)) != 0)) {
            std::size_t end = at + 1U;
            while (end < source.size() && (IsNameChar(source[end]) || source[end] == '.' ||
                       ((source[end] == '+' || source[end] == '-') && std::string_view{ "eEpP" }.find(source[end - 1U]) != std::string_view::npos))) {
                ++end;
            }
            token.kind = TokenKind::Number;
            token.text = source.substr(at, end - at);
            advance(end);
        } else {
            std::size_t length = 1U;
            if (source.compare(at, 3U, "...") == 0) length = 3U;
            else if (const std::string_view two = source.substr(at, 2U); two == ".." || two == "==" || two == "~=" || two == "<=" || two == ">=" || two == "::") length = 2U;
            token.text = source.substr(at, length);
            advance(at + length);
        }
        token.range.end = here();
        tokens.push_back(token);
    }
    return tokens;
}

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// A block waiting for its `end` or `until`: a function, `if` or `do` block; a loop whose `do` has not come yet; a
// `repeat`. `declaration` is the named function the block closes.
enum class BlockKind { End, LoopHeader, Repeat };
struct Block {
    BlockKind kind;
    std::size_t declaration;
};

} // namespace

LuaSourceIndex IndexLuaSource(std::string_view source) {
    const std::vector<Token> t = Tokenize(source);
    LuaSourceIndex index;
    std::vector<Block> open;
    int brackets = 0;

    const auto isName = [&](std::size_t i, std::string_view text) {
        return i < t.size() && t[i].kind == TokenKind::Name && t[i].text == text;
    };
    const auto isSymbol = [&](std::size_t i, std::string_view text) {
        return i < t.size() && t[i].kind == TokenKind::Symbol && t[i].text == text;
    };
    const auto afterMember = [&](std::size_t i) { return i > 0U && (isSymbol(i - 1U, ".") || isSymbol(i - 1U, ":")); };

    // The declaration made by the `function` at `i`, or kNone for a function that has no name.
    const auto declareFunction = [&](std::size_t i) {
        LuaDeclaration declaration;
        declaration.isFunction = true;
        declaration.atTopLevel = open.empty() && brackets == 0;
        std::size_t cursor = i + 1U;
        if (cursor < t.size() && t[cursor].kind == TokenKind::Name) { // function Name{.Name}[:Name](
            std::size_t name = cursor;
            while (name + 2U < t.size() && (isSymbol(name + 1U, ".") || isSymbol(name + 1U, ":")) && t[name + 2U].kind == TokenKind::Name) {
                declaration.qualifier += (declaration.qualifier.empty() ? "" : ".") + std::string{ t[name].text };
                name += 2U;
            }
            declaration.name = std::string{ t[name].text };
            declaration.nameRange = t[name].range;
            declaration.range.begin = i > 0U && isName(i - 1U, "local") ? t[i - 1U].range.begin : t[i].range.begin;
            cursor = name + 1U;
        } else if (i >= 2U && isSymbol(i - 1U, "=") && t[i - 2U].kind == TokenKind::Name) { // [local] Name{.Name} = function(
            std::size_t name = i - 2U;
            declaration.name = std::string{ t[name].text };
            declaration.nameRange = t[name].range;
            while (name >= 2U && isSymbol(name - 1U, ".") && t[name - 2U].kind == TokenKind::Name) {
                declaration.qualifier = std::string{ t[name - 2U].text } + (declaration.qualifier.empty() ? "" : ".") + declaration.qualifier;
                name -= 2U;
            }
            declaration.range.begin = name > 0U && isName(name - 1U, "local") ? t[name - 1U].range.begin : t[name].range.begin;
        } else {
            return kNone;
        }
        if (isSymbol(cursor, "(")) {
            for (++cursor; cursor < t.size() && !isSymbol(cursor, ")"); ++cursor) {
                if (t[cursor].kind == TokenKind::Name || isSymbol(cursor, "...")) declaration.parameters.emplace_back(t[cursor].text);
            }
        }
        declaration.range.end = declaration.nameRange.end;
        index.declarations.push_back(std::move(declaration));
        return index.declarations.size() - 1U;
    };

    const auto addVariable = [&](const Token& name) {
        LuaDeclaration declaration;
        declaration.name = std::string{ name.text };
        declaration.atTopLevel = true;
        declaration.nameRange = name.range;
        declaration.range = name.range;
        index.declarations.push_back(std::move(declaration));
    };

    for (std::size_t i = 0U; i < t.size(); ++i) {
        const Token& token = t[i];
        if (token.kind == TokenKind::Symbol) {
            if (token.text == "(" || token.text == "{" || token.text == "[") ++brackets;
            else if (token.text == ")" || token.text == "}" || token.text == "]") brackets = std::max(0, brackets - 1);
            continue;
        }
        if (token.kind != TokenKind::Name || afterMember(i)) continue;
        const std::string_view word = token.text;
        if (word == "function") {
            open.push_back({ BlockKind::End, declareFunction(i) });
        } else if (word == "if") {
            open.push_back({ BlockKind::End, kNone });
        } else if (word == "for" || word == "while") {
            open.push_back({ BlockKind::LoopHeader, kNone });
        } else if (word == "do") {
            if (!open.empty() && open.back().kind == BlockKind::LoopHeader) open.back().kind = BlockKind::End;
            else open.push_back({ BlockKind::End, kNone });
        } else if (word == "repeat") {
            open.push_back({ BlockKind::Repeat, kNone });
        } else if (word == "until") {
            if (!open.empty() && open.back().kind == BlockKind::Repeat) open.pop_back();
        } else if (word == "end") {
            if (open.empty()) continue;
            if (open.back().declaration != kNone) index.declarations[open.back().declaration].range.end = token.range.end;
            open.pop_back();
        } else if (word == "Import" || word == "require") {
            std::size_t argument = i + 1U;
            if (isSymbol(argument, "(")) ++argument;
            if (argument < t.size() && t[argument].kind == TokenKind::String) {
                LuaImport import;
                import.module = std::string{ t[argument].text };
                import.moduleRange = t[argument].range;
                if (i >= 2U && isSymbol(i - 1U, "=") && t[i - 2U].kind == TokenKind::Name && !afterMember(i - 2U)) import.alias = std::string{ t[i - 2U].text };
                index.imports.push_back(std::move(import));
            }
        } else if (word == "local" && open.empty() && brackets == 0) {
            // local a, b = ...: a function stored in a local is declared by its `function`.
            std::size_t last = i + 1U;
            std::vector<const Token*> names;
            while (last < t.size() && t[last].kind == TokenKind::Name && t[last].text != "function") {
                names.push_back(&t[last]);
                if (!isSymbol(last + 1U, ",")) break;
                last += 2U;
            }
            if (!names.empty() && !(isSymbol(last + 1U, "=") && isName(last + 2U, "function"))) {
                for (const Token* name : names) addVariable(*name);
            }
            if (!names.empty()) i = last;
        } else if (open.empty() && brackets == 0 && isSymbol(i + 1U, "=") && !isName(i + 2U, "function")) {
            addVariable(token);
        }
    }
    return index;
}

} // namespace kb::cli
