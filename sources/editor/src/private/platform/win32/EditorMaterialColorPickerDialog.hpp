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
    [[nodiscard]] static std::optional<std::array<float, 4U>> Show(
        HWND owner,
        std::string_view title,
        const std::array<float, 4U>& currentColor);
#endif
};

} // namespace kb::editor
