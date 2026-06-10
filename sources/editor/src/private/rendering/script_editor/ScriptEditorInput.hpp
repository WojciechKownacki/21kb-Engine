#pragma once

#include "rendering/script_editor/ScriptEditorLayout.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class ScriptEditorDocument;

#if defined(_WIN32)

struct ScriptEditorInputResult {
    bool changed = false;      // The document or viewport changed (repaint needed).
    bool saveRequested = false; // Ctrl+S was pressed (host should persist).
};

// Translates keyboard input into document edits / caret movement, handling
// clipboard, undo/redo and selection. Owns no state: it mutates the document and
// viewport it is given. The host performs persistence and repainting.
class ScriptEditorInput {
public:
    ScriptEditorInput() = delete;

    [[nodiscard]] static ScriptEditorInputResult HandleKeyDown(HWND window, ScriptEditorDocument& document, ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, WPARAM key);
    [[nodiscard]] static bool HandleChar(HWND window, ScriptEditorDocument& document, ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, wchar_t character);
};

#endif

} // namespace kb::editor
