#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <filesystem>

namespace kb::editor {

#if defined(_WIN32)

// The Win32 child window that hosts the code editor. It owns the document, the
// viewport and the loaded file, and wires window messages to the document /
// renderer / input components. Persistence is a plain file write on Ctrl+S.
class ScriptEditorWindow {
public:
    ScriptEditorWindow() = delete;

    // Creates (once) the editor child window under parent and returns it.
    [[nodiscard]] static HWND Ensure(HWND parent);

    // Repositions the window (only when the rect changes) and reloads the file
    // whenever generation changes.
    static void Sync(HWND window, const RECT& bounds, const std::filesystem::path& filePath, std::uint64_t generation);

    static void Hide(HWND window) noexcept;
    [[nodiscard]] static bool IsModified(HWND window);
};

#endif

} // namespace kb::editor
