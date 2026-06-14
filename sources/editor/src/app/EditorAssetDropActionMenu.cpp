#include "app/EditorAssetDropActionMenu.hpp"

#if defined(_WIN32)

namespace kb::editor {
namespace {

constexpr UINT_PTR kDropActionMoveHere = 3001;
constexpr UINT_PTR kDropActionCopyHere = 3002;

} // namespace

EditorAssetDropAction EditorAssetDropActionMenu::Show(HWND window, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return EditorAssetDropAction::None;
    }

    AppendMenuA(menu, MF_STRING, kDropActionMoveHere, "Move Here");
    AppendMenuA(menu, MF_STRING, kDropActionCopyHere, "Copy Here");

    POINT screenPoint{ x, y };
    ClientToScreen(window, &screenPoint);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);

    switch (command) {
    case kDropActionMoveHere:
        return EditorAssetDropAction::MoveHere;
    case kDropActionCopyHere:
        return EditorAssetDropAction::CopyHere;
    default:
        return EditorAssetDropAction::None;
    }
}

} // namespace kb::editor

#endif
