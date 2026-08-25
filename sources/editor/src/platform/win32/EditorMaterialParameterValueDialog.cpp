#include "platform/win32/EditorMaterialParameterValueDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorTextEntryDialog.hpp"

#include <string>

namespace kb::editor {

std::optional<std::string> EditorMaterialParameterValueDialog::Show(
    HWND owner,
    std::string_view parameterName,
    std::string_view currentValue,
    std::string_view hint) {
    return EditorTextEntryDialog::Show(owner, EditorTextEntryDialogDescriptor{
        .title = "Edit Value",
        .label = "Value for " + std::string{parameterName},
        .value = std::string{currentValue},
        .hint = std::string{hint},
        .acceptLabel = "Apply",
        .icon = HeroIconKind::AdjustmentsHorizontal,
    });
}

} // namespace kb::editor

#endif
