#pragma once

#include "engine/scene/SceneEntity.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

enum class EditorSceneViewportSelectionMode {
    Replace,
    Toggle,
};

struct EditorSceneViewportPickResult {
    kb::scene::SceneEntity entity{};
    float distance = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept {
        return entity.IsValid();
    }
};

struct EditorSceneViewportBoxSelectionState {
    bool pending = false;
    bool active = false;
    bool additive = false;
    std::uint32_t panelId = 0;
    POINT start{};
    POINT current{};
    RECT renderArea{};
};

#endif

} // namespace kb::editor
