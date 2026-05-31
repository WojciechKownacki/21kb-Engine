#pragma once

#include <cstddef>
#include <string>

namespace kb::scene {

class Scene;

class SceneHistory {
public:
    explicit SceneHistory(Scene& scene) noexcept;

    [[nodiscard]] bool Record(std::string label = {});
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    void Clear() noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
