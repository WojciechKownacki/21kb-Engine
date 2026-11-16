#pragma once

#include <string>

namespace kb::editor {

// One warning about the script text: where it is (zero-based line and byte column, like ScriptCaret) and what it says.
struct ScriptProblem {
    int line = 0;
    int column = 0;
    int length = 1;
    std::string message;

    [[nodiscard]] bool operator==(const ScriptProblem&) const = default;
};

} // namespace kb::editor
