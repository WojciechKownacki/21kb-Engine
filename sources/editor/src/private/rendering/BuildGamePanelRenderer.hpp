#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

// Paints the Build Game panel: the build targets and profiles on the left, the selected
// target's project, application, content, signing and output settings on the right, and
// the status line and build action along the foot.
//
// Presentation only for now. Nothing here reads or writes project state, and no row
// responds to the pointer; the panel shows the shape of a build and the values it will
// ask for, and the packaging service is wired to it separately.
class BuildGamePanelRenderer {
public:
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, EditorSceneContext& sceneContext) const;

    // Height of the settings column at full extent. The row table lives here, so the
    // wheel route asks the panel rather than restating the sections.
    [[nodiscard]] static int SettingsContentHeight() noexcept;
};

#endif

} // namespace kb::editor
