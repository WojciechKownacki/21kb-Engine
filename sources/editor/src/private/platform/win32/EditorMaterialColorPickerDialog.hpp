#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <optional>
#include <string_view>

namespace kb::editor {

class EditorMaterialColorPickerDialog final {
public:
    EditorMaterialColorPickerDialog() = delete;

#if defined(_WIN32)
    // `anchorScreenPoint` places the picker at the swatch the user clicked instead of the middle of the
    // screen; pass nullptr to keep it centred on the owner.
    [[nodiscard]] static std::optional<std::array<float, 4U>> Show(
        HWND owner,
        std::string_view title,
        const std::array<float, 4U>& currentColor,
        const POINT* anchorScreenPoint = nullptr);
#endif
};

} // namespace kb::editor
