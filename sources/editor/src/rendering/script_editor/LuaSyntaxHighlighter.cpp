#include "rendering/script_editor/LuaSyntaxHighlighter.hpp"

#include <unordered_set>

namespace kb::editor {
namespace {

[[nodiscard]] const std::unordered_set<std::string_view>& Keywords() {
    static const std::unordered_set<std::string_view> keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
        "true", "until", "while",
    };
    return keywords;
}

[[nodiscard]] bool IsIdentChar(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

[[nodiscard]] bool IsIdentStart(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

[[nodiscard]] bool IsDigit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

} // namespace

std::vector<ScriptToken> LuaSyntaxHighlighter::Tokenize(std::string_view line) {
    std::vector<ScriptToken> tokens;
    const int length = static_cast<int>(line.size());
    int index = 0;
    while (index < length) {
        const char ch = line[static_cast<std::size_t>(index)];
        if (ch == '-' && index + 1 < length && line[static_cast<std::size_t>(index) + 1] == '-') {
            tokens.push_back(ScriptToken{ index, length - index, ScriptTokenKind::Comment });
            break;
        }
        if (ch == '"' || ch == '\'') {
            const int start = index;
            const char quote = ch;
            ++index;
            while (index < length) {
                const char inner = line[static_cast<std::size_t>(index)];
                if (inner == '\\' && index + 1 < length) {
                    index += 2;
                    continue;
                }
                ++index;
                if (inner == quote) {
                    break;
                }
            }
            tokens.push_back(ScriptToken{ start, index - start, ScriptTokenKind::String });
            continue;
        }
        if (IsDigit(ch) || (ch == '.' && index + 1 < length && IsDigit(line[static_cast<std::size_t>(index) + 1]))) {
            const int start = index;
            while (index < length && (IsIdentChar(line[static_cast<std::size_t>(index)]) || line[static_cast<std::size_t>(index)] == '.')) {
                ++index;
            }
            tokens.push_back(ScriptToken{ start, index - start, ScriptTokenKind::Number });
            continue;
        }
        if (IsIdentStart(ch)) {
            const int start = index;
            while (index < length && IsIdentChar(line[static_cast<std::size_t>(index)])) {
                ++index;
            }
            const std::string_view word = line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(index - start));
            ScriptTokenKind kind = ScriptTokenKind::Default;
            if (Keywords().contains(word)) {
                kind = ScriptTokenKind::Keyword;
            } else {
                int probe = index;
                while (probe < length && line[static_cast<std::size_t>(probe)] == ' ') {
                    ++probe;
                }
                if (probe < length && line[static_cast<std::size_t>(probe)] == '(') {
                    kind = ScriptTokenKind::Function;
                }
            }
            tokens.push_back(ScriptToken{ start, index - start, kind });
            continue;
        }
        tokens.push_back(ScriptToken{ index, 1, ScriptTokenKind::Default });
        ++index;
    }
    return tokens;
}

} // namespace kb::editor
