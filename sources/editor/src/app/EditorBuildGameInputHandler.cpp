#include "app/EditorBuildGameInputHandler.hpp"

#include "app/EditorTextInputShortcuts.hpp"
#include "rendering/BuildGamePanelModel.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
namespace kb::editor {

EditorBuildGameInputHandler::EditorBuildGameInputHandler(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorBuildGameInputHandler::HandleCharacter(wchar_t character) const {
    return sceneContext_.AppendBuildGameText(character);
}

bool EditorBuildGameInputHandler::HandleKeyDown(HWND owner, WPARAM key) const {
    if (!sceneContext_.IsBuildGameTextEditing()) return false;
    const BuildGameField field = sceneContext_.BuildGameEditingField();
    const bool sensitive = field == BuildGameField::AndroidStorePassword || field == BuildGameField::AndroidKeyPassword;
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::SelectAll:
        static_cast<void>(sceneContext_.SelectAllBuildGameText());
        return true;
    case EditorTextInputShortcut::Copy:
        if (!sensitive) {
            static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext_.BuildGameEditBuffer()));
        }
        return true;
    case EditorTextInputShortcut::Cut:
        if (!sensitive && EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext_.BuildGameEditBuffer())) {
            static_cast<void>(sceneContext_.SelectAllBuildGameText());
            static_cast<void>(sceneContext_.BackspaceBuildGameText());
        }
        return true;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(
                owner, sensitive ? 512U : 4096U); text.has_value()) {
            static_cast<void>(sceneContext_.InsertBuildGameText(*text));
        }
        return true;
    case EditorTextInputShortcut::None:
        break;
    }
    switch (key) {
    case VK_RETURN:
        static_cast<void>(sceneContext_.CommitBuildGameTextEdit());
        return true;
    case VK_ESCAPE:
        sceneContext_.CancelBuildGameTextEdit();
        return true;
    case VK_TAB:
        static_cast<void>(sceneContext_.FocusAdjacentBuildGameTextField(
            (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return true;
    case VK_BACK:
        static_cast<void>(sceneContext_.BackspaceBuildGameText());
        return true;
    default:
        break;
    }
    return false;
}

} // namespace kb::editor
#endif
