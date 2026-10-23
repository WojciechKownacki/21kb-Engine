#pragma once

#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "scene/EditorPlayModeSelectionSnapshot.hpp"

#include <optional>
#include <string>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorPlayModeSceneSession {
public:
    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] bool Begin(kb::scene::Scene& scene, std::string sceneName);
    [[nodiscard]] bool Restore(kb::scene::Scene& scene);
    [[nodiscard]] std::span<const kb::scene::SceneEntityRemap> RestoredEntities() const noexcept;
    void Clear() noexcept;

private:
    std::optional<kb::scene::SceneDocument> snapshot_;
    kb::scene::Scene* owner_ = nullptr;
    kb::scene::SceneHistorySnapshot history_;
    EditorPlayModeSelectionSnapshot hierarchy_;
    std::vector<kb::scene::SceneEntityRemap> restoredEntities_;
};

} // namespace kb::editor
