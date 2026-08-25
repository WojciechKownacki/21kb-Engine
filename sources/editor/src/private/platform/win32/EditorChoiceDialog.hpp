#pragma once

#include "rendering/HeroIconKind.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>

namespace kb::editor {

#if defined(_WIN32)

enum class EditorChoiceDialogResult {
    Primary,
    Secondary,
    Cancel,
};

struct EditorChoiceDialogDescriptor {
    std::string title;
    std::string message;
    std::string supportingText;
    std::string primaryLabel = "OK";
    std::string secondaryLabel;
    std::string cancelLabel;
    HeroIconKind icon = HeroIconKind::DocumentText;
    EditorDialogButtonTone primaryTone = EditorDialogButtonTone::Primary;
};

class EditorChoiceDialog final {
public:
    EditorChoiceDialog() = delete;

    [[nodiscard]] static EditorChoiceDialogResult Show(
        HWND owner,
        EditorChoiceDialogDescriptor descriptor);
};

#endif

} // namespace kb::editor
