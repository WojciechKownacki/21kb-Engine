#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// Semantic classification of a span of characters on one line. Colours are
// applied later by the theme, keeping the highlighter free of any UI concern.
enum class ScriptTokenKind {
    Default,
    Keyword,
    String,
    Comment,
    Number,
    Function,
};

struct ScriptToken {
    int start = 0;
    int length = 0;
    ScriptTokenKind kind = ScriptTokenKind::Default;
};

// Pure, single-line Lua tokenizer: line comments, single-line strings, numbers,
// keywords and function-call identifiers. No Win32, no allocation of state
// across lines, so it is trivially testable and fast. Multi-line strings /
// block comments are classified per line.
class LuaSyntaxHighlighter {
public:
    LuaSyntaxHighlighter() = delete;

    [[nodiscard]] static std::vector<ScriptToken> Tokenize(std::string_view line);
};

} // namespace kb::editor
