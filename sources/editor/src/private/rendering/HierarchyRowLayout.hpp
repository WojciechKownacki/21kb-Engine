#pragma once

#include "scene/EditorHierarchyRow.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct HierarchyRowLayoutRects {
    RECT visibilityCell{};
    RECT visibilityIcon{};
    RECT expanderHit{};
    RECT expanderIcon{};
    RECT entityIcon{};
    RECT label{};
};

class HierarchyRowLayout {
public:
    HierarchyRowLayout() = delete;

    [[nodiscard]] static HierarchyRowLayoutRects Resolve(const RECT& rowRect, const EditorHierarchyRow& row) noexcept;
};

#endif

} // namespace kb::editor
