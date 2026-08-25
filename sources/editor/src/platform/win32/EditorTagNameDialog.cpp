#include "platform/win32/EditorTagNameDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorTextEntryDialog.hpp"

namespace kb::editor {

std::optional<std::string> EditorTagNameDialog::Show(HWND owner) {
    return EditorTextEntryDialog::Show(owner, EditorTextEntryDialogDescriptor{
        .title = "New Tag",
        .label = "Tag name",
        .hint = "Names cannot contain commas or semicolons.",
        .acceptLabel = "Add",
        .icon = HeroIconKind::Plus,
    });
}

} // namespace kb::editor

#endif
