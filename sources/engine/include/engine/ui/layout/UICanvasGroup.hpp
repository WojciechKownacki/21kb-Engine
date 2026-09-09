#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UICanvasGroup {
    static constexpr std::string_view StableId = "kb21.ui.canvas-group";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float opacity = 1.0F;
    bool interactable = true;
    bool blocksRaycasts = true;
    bool ignoreParentGroups = false;
};

} // namespace kb::scene
