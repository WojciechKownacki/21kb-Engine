#pragma once

#include <string>
#include <utility>

namespace kb::editor {

class EditorHeadlessAutomation;

#if defined(_WIN32)

// The startup screen checked without showing it: its layout at 100 % and 200 % DPI, the bar's fill
// growing with the progress, and every stage's text fitting its rectangle in every editor language.
// It also paints the screen at 0 %, about 50 % with a long scene name, and 100 % into the
// automation's screenshots folder.
class EditorStartupSplashChecks {
public:
    EditorStartupSplashChecks() = delete;

    [[nodiscard]] static std::pair<bool, std::string> Run(EditorHeadlessAutomation& automation);
};

#endif

} // namespace kb::editor
