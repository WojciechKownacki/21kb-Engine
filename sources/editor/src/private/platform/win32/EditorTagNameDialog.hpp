#pragma once

#include <optional>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorTagNameDialog final {
public:
    EditorTagNameDialog() = delete;

#if defined(_WIN32)
    [[nodiscard]] static std::optional<std::string> Show(HWND owner);
#endif
};

} // namespace kb::editor
