#pragma once

namespace kb::editor {

// Key and mouse-button state for editor input handlers. Scripted automation
// (self-test and headless scenarios) runs while someone may still be typing on
// the desktop; Windows reports that physical state to background processes, so
// a held Shift or Alt would silently change what an automated edit does. While
// any scripted-input scope is alive every key reads as released.
[[nodiscard]] bool EditorKeyDown(int virtualKey) noexcept;
[[nodiscard]] bool EditorAsyncKeyDown(int virtualKey) noexcept;

class EditorScriptedInputScope final {
public:
    EditorScriptedInputScope() noexcept;
    ~EditorScriptedInputScope();
    EditorScriptedInputScope(const EditorScriptedInputScope&) = delete;
    EditorScriptedInputScope& operator=(const EditorScriptedInputScope&) = delete;
};

// A scoped key held down for messages sent by deterministic headless controls; scopes nest, so a few keys can
// be held at once.
class EditorScriptedKeyScope final {
public:
    explicit EditorScriptedKeyScope(int key) noexcept;
    ~EditorScriptedKeyScope();
    EditorScriptedKeyScope(const EditorScriptedKeyScope&) = delete;
    EditorScriptedKeyScope& operator=(const EditorScriptedKeyScope&) = delete;
private:
    int slot_ = -1;
};

} // namespace kb::editor
