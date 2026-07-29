#include "engine/gameplay/GameFlow.hpp"

namespace kb::gameplay {
bool GameFlow::Pause() noexcept { if(state_!=GameFlowState::Playing)return false; state_=GameFlowState::Paused; return true; }
bool GameFlow::Resume() noexcept { if(state_!=GameFlowState::Paused)return false; state_=GameFlowState::Playing; return true; }
bool GameFlow::Win() noexcept { if(state_!=GameFlowState::Playing)return false; state_=GameFlowState::Won; return true; }
bool GameFlow::Lose() noexcept { if(state_!=GameFlowState::Playing)return false; state_=GameFlowState::Lost; return true; }
bool GameFlow::SetCheckpoint(GameSceneId scene) noexcept { if(scene==0U)return false; checkpoint_=scene; return true; }
std::optional<GameSceneId> GameFlow::Restart() noexcept { if(!checkpoint_.has_value())return std::nullopt; state_=GameFlowState::Transitioning; pendingTransition_=checkpoint_; return pendingTransition_; }
bool GameFlow::BeginTransition(GameSceneId destination) noexcept { if(destination==0U||state_==GameFlowState::Transitioning)return false; pendingTransition_=destination; state_=GameFlowState::Transitioning; return true; }
bool GameFlow::CompleteTransition() noexcept { if(state_!=GameFlowState::Transitioning||!pendingTransition_.has_value())return false; pendingTransition_.reset(); state_=GameFlowState::Playing; return true; }
void GameFlow::ClearScene(GameSceneId scene) noexcept { if(checkpoint_==scene)checkpoint_.reset(); if(pendingTransition_==scene){pendingTransition_.reset(); state_=GameFlowState::Playing;} }
} // namespace kb::gameplay
