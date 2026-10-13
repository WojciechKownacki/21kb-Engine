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

// Text that is data, not interface: a log message or a path the user made. Its row ends it with an
// ellipsis and shows it whole elsewhere (Unity's console: the row, then the detail pane), so a
// translation check must not count it as cut. Pauses the record for its scope.
class CutTextDataScope {
public:
    CutTextDataScope() noexcept : saved_(CutTextSink()) { CutTextSink() = nullptr; }
    ~CutTextDataScope() { CutTextSink() = saved_; }
    CutTextDataScope(const CutTextDataScope&) = delete;
    CutTextDataScope& operator=(const CutTextDataScope&) = delete;

private:
    std::vector<CutText>* saved_;
};

} // namespace kb::editor
