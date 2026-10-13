#include "platform/win32/EditorTagNameDialog.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorTextEntryDialog.hpp"

namespace kb::editor {

std::optional<std::string> EditorTagNameDialog::Show(HWND owner) {
    return EditorTextEntryDialog::Show(owner, EditorTextEntryDialogDescriptor{
        .title = std::string{ EditorText("tag.newTitle", "New Tag") },
        .label = std::string{ EditorText("tag.nameLabel", "Tag name") },
        .hint = std::string{ EditorText("tag.nameHint", "Names cannot contain commas or semicolons.") },
        .acceptLabel = std::string{ EditorText("common.add", "Add") },
        .icon = HeroIconKind::Plus,
    });
}

} // namespace kb::editor

#endif
