#pragma once

#include <cstdint>
#include <string>

namespace kb::editor {

/// Generic, dependency-free value formatters shared across editor panels.
/// Kept separate from panel-specific formatting so renderers do not duplicate
/// the same float/integer presentation logic.
class EditorValueFormatter {
public:
    EditorValueFormatter() = delete;

    /// Fixed-precision float with trailing zeros trimmed ("-0" normalized to "0").
    [[nodiscard]] static std::string FormatFloat(float value, int precision = 3);
    /// Unsigned 64-bit integer as plain text.
    [[nodiscard]] static std::string FormatUInt64(std::uint64_t value);
};

} // namespace kb::editor
