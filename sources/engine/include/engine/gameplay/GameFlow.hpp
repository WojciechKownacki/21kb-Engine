#pragma once

#include <cstdint>
#include <optional>

namespace kb::gameplay {

using GameSceneId = std::uint64_t;

enum class GameFlowState : std::uint8_t { Playing, Paused, Won, Lost, Transitioning };

class GameFlow final {
public:
    [[nodiscard]] GameFlowState State() const noexcept { return state_; }
    [[nodiscard]] bool Pause() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    [[nodiscard]] bool Win() noexcept;
    [[nodiscard]] bool Lose() noexcept;
    [[nodiscard]] bool SetCheckpoint(GameSceneId scene) noexcept;
    [[nodiscard]] std::optional<GameSceneId> Checkpoint() const noexcept { return checkpoint_; }
    [[nodiscard]] std::optional<GameSceneId> Restart() noexcept;
    [[nodiscard]] bool BeginTransition(GameSceneId destination) noexcept;
    [[nodiscard]] std::optional<GameSceneId> PendingTransition() const noexcept { return pendingTransition_; }
    [[nodiscard]] bool CompleteTransition() noexcept;
    void ClearScene(GameSceneId scene) noexcept;

private:
    GameFlowState state_ = GameFlowState::Playing;
    std::optional<GameSceneId> checkpoint_;
    std::optional<GameSceneId> pendingTransition_;
};

} // namespace kb::gameplay
