#include "app/scene_viewport/EditorTerrainFileDialog.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <array>
#include <string>

namespace kb::editor {

std::optional<std::filesystem::path> EditorTerrainFileDialog::PickHeightImage(HWND owner, std::string_view title) {
    std::array<wchar_t, 32768U> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    const std::wstring filter = EditorFileDialogFilter({
        { EditorText("fileDialog.heightmapFilter", "Heightmaps (*.png;*.raw;*.r16)"), "*.png;*.raw;*.r16" },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
    const std::wstring wideTitle = ScriptEditorTextEncoding::Widen(title);
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = wideTitle.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) != FALSE ? std::optional<std::filesystem::path>{ path.data() } : std::nullopt;
}

} // namespace kb::editor

#endif
