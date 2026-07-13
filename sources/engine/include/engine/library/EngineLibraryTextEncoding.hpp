#pragma once

#include <string_view>

namespace kb::library {

// LIB-064: UTF-8 is the only encoding a public string value in this engine
// may use. IsValidUtf8 rejects everything that is not well-formed UTF-8
// per the Unicode standard's own validation table — overlong encodings
// (e.g. 0xC0 0x80 for NUL), encoded UTF-16 surrogate halves (U+D800..
// U+DFFF, which UTF-8 must never represent), codepoints beyond U+10FFFF,
// truncated multi-byte sequences, and stray continuation/invalid lead
// bytes — not just "does every byte look plausible".
//
// Wired into kb::script::ScriptFunctionRegistry::ValidateInputs, the same
// choke point LIB-037's string-length limit already uses for every String
// argument crossing the script boundary (Native, Lua, Visual Graph alike)
// — the one place a public string value is guaranteed to pass through, so
// this is where "UTF-8 as the only format" is actually enforced, not just
// documented.
[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;

} // namespace kb::library
