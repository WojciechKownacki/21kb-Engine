#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorMaterialParameterValueDialog final {
public:
    EditorMaterialParameterValueDialog() = delete;

#if defined(_WIN32)
    [[nodiscard]] static std::optional<std::string> Show(
        HWND owner,
        std::string_view parameterName,
        std::string_view currentValue);
#endif
};

} // namespace kb::editor
