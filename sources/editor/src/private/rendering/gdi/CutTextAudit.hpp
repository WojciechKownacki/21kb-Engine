#pragma once

#include <string>
#include <vector>

namespace kb::editor {

// A single-line text drawn wider than the room it has: GDI cuts it (or ends it with an ellipsis).
struct CutText {
    std::string text;
    int needed = 0;
    int available = 0;
};

// Off unless a headless scenario turns it on (capture with "text_fits"): then DrawTextWide
// (platform/win32/Utf8Gdi.hpp) records every cut text here - a translation longer than the
// English must fit too.
[[nodiscard]] inline std::vector<CutText>*& CutTextSink() noexcept {
    static std::vector<CutText>* sink = nullptr;
    return sink;
}

} // namespace kb::editor
