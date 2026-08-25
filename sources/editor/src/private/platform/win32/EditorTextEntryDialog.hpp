#pragma once

#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>
#include <string>

namespace kb::editor {

#if defined(_WIN32)

struct EditorTextEntryDialogDescriptor {
    std::string title;
    std::string label;
    std::string value;
    std::string hint;
    std::string acceptLabel = "Apply";
    HeroIconKind icon = HeroIconKind::DocumentText;
};

class EditorTextEntryDialog final {
public:
    EditorTextEntryDialog() = delete;

    [[nodiscard]] static std::optional<std::string> Show(
        HWND owner,
        EditorTextEntryDialogDescriptor descriptor);
};

#endif

} // namespace kb::editor
