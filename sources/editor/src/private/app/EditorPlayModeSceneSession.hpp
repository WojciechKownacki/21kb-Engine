#pragma once

#include "engine/scene/SceneDocument.hpp"

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
    void Clear() noexcept;

private:
    std::optional<kb::scene::SceneDocument> snapshot_;
};

} // namespace kb::editor
