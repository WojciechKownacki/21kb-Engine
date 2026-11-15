#pragma once

#include "engine/scene/SceneTemplates.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

// File > New Project, first step: the project templates as a list with a line of description under each name. The
// second step is the dialog that names the project and chooses its place. nullopt = cancelled.
class EditorProjectTemplateDialog final {
public:
    EditorProjectTemplateDialog() = delete;

    [[nodiscard]] static std::optional<kb::scene::SceneTemplate> Show(HWND owner);
};

#endif

} // namespace kb::editor
