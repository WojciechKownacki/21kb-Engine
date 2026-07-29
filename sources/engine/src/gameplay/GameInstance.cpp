#include "engine/gameplay/GameInstance.hpp"

#include "engine/scene/Scene.hpp"

#include <algorithm>

namespace kb::gameplay {

GameInstance::GameInstance(kb::project::ProjectDescriptor project) : project_(std::move(project)) {}
GameInstance::~GameInstance() = default;
GameSceneId GameInstance::CreateScene(kb::scene::SceneMode mode) { const GameSceneId id = nextSceneId_++; scenes_.push_back({ .id = id, .scene = std::make_unique<kb::scene::Scene>(project_, mode) }); if (activeScene_ == 0U) activeScene_ = id; return id; }
bool GameInstance::DestroyScene(GameSceneId id) noexcept { const auto it = std::find_if(scenes_.begin(), scenes_.end(), [id](const Entry& entry){ return entry.id == id; }); if (it == scenes_.end()) return false; scenes_.erase(it); flow_.ClearScene(id); if (activeScene_ == id) activeScene_ = scenes_.empty() ? 0U : scenes_.front().id; return true; }
kb::scene::Scene* GameInstance::FindScene(GameSceneId id) noexcept { const auto it = std::find_if(scenes_.begin(), scenes_.end(), [id](const Entry& entry){ return entry.id == id; }); return it == scenes_.end() ? nullptr : it->scene.get(); }
const kb::scene::Scene* GameInstance::FindScene(GameSceneId id) const noexcept { return const_cast<GameInstance*>(this)->FindScene(id); }
bool GameInstance::SetActiveScene(GameSceneId id) noexcept { if (FindScene(id) == nullptr) return false; activeScene_ = id; return true; }
bool GameInstance::TransitionToScene(GameSceneId id) noexcept { if (FindScene(id) == nullptr) return false; if(!flow_.PendingTransition().has_value() && !flow_.BeginTransition(id))return false; return flow_.PendingTransition()==id && SetActiveScene(id) && flow_.CompleteTransition(); }
bool GameInstance::SetCheckpoint(GameSceneId id) noexcept { return FindScene(id) != nullptr && flow_.SetCheckpoint(id); }
bool GameInstance::Pause() noexcept { return flow_.Pause(); }
bool GameInstance::Resume() noexcept { return flow_.Resume(); }
bool GameInstance::Win() noexcept { return flow_.Win(); }
bool GameInstance::Lose() noexcept { return flow_.Lose(); }
bool GameInstance::RestartFromCheckpoint() noexcept { const std::optional<GameSceneId> checkpoint = flow_.Restart(); return checkpoint.has_value() && TransitionToScene(*checkpoint); }
GameSceneId GameInstance::ActiveSceneId() const noexcept { return activeScene_; }
kb::scene::Scene* GameInstance::ActiveScene() noexcept { return FindScene(activeScene_); }
const kb::scene::Scene* GameInstance::ActiveScene() const noexcept { return FindScene(activeScene_); }
std::size_t GameInstance::SceneCount() const noexcept { return scenes_.size(); }

} // namespace kb::gameplay
