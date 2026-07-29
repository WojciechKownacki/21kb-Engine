#pragma once

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/gameplay/GameMode.hpp"
#include "engine/gameplay/GameState.hpp"
#include "engine/gameplay/Players.hpp"
#include "engine/gameplay/CameraManager.hpp"
#include "engine/gameplay/MatchConfig.hpp"
#include "engine/save/SaveGame.hpp"
#include "engine/scene/SceneMode.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace kb::scene { class Scene; }

namespace kb::gameplay {

using GameSceneId = std::uint64_t;

// Owns services whose lifetime is deliberately longer than every individual
// Scene. Scene-local assets, input and module hosts remain owned by Scene;
// only cross-scene game progression/settings live here.
struct GameInstanceServices {
    kb::save::SaveGame progression;
    kb::save::SaveGame settings;
};

class GameInstance final {
public:
    explicit GameInstance(kb::project::ProjectDescriptor project = {});
    ~GameInstance();
    GameInstance(const GameInstance&) = delete;
    GameInstance& operator=(const GameInstance&) = delete;

    [[nodiscard]] GameSceneId CreateScene(kb::scene::SceneMode mode = kb::scene::SceneMode::Runtime);
    [[nodiscard]] bool DestroyScene(GameSceneId id) noexcept;
    [[nodiscard]] kb::scene::Scene* FindScene(GameSceneId id) noexcept;
    [[nodiscard]] const kb::scene::Scene* FindScene(GameSceneId id) const noexcept;
    [[nodiscard]] bool SetActiveScene(GameSceneId id) noexcept;
    [[nodiscard]] GameSceneId ActiveSceneId() const noexcept;
    [[nodiscard]] kb::scene::Scene* ActiveScene() noexcept;
    [[nodiscard]] const kb::scene::Scene* ActiveScene() const noexcept;
    [[nodiscard]] std::size_t SceneCount() const noexcept;
    [[nodiscard]] GameInstanceServices& Services() noexcept { return services_; }
    [[nodiscard]] const GameInstanceServices& Services() const noexcept { return services_; }
    void SetGameMode(GameMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] GameMode& Mode() noexcept { return mode_; }
    [[nodiscard]] const GameMode& Mode() const noexcept { return mode_; }
    [[nodiscard]] GameState& State() noexcept { return state_; }
    [[nodiscard]] const GameState& State() const noexcept { return state_; }
    [[nodiscard]] Players& PlayerRegistry() noexcept { return players_; }
    [[nodiscard]] const Players& PlayerRegistry() const noexcept { return players_; }
    [[nodiscard]] CameraManager& Cameras() noexcept { return cameras_; }
    [[nodiscard]] MatchRuntime& Match() noexcept { return match_; }

private:
    struct Entry { GameSceneId id = 0U; std::unique_ptr<kb::scene::Scene> scene; };
    kb::project::ProjectDescriptor project_;
    GameInstanceServices services_;
    GameMode mode_;
    GameState state_;
    Players players_;
    CameraManager cameras_;
    MatchRuntime match_;
    std::vector<Entry> scenes_;
    GameSceneId nextSceneId_ = 1U;
    GameSceneId activeScene_ = 0U;
};

} // namespace kb::gameplay
