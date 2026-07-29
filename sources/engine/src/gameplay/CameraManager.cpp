#include "engine/gameplay/CameraManager.hpp"
namespace kb::gameplay {
bool CameraManager::SetView(PlayerId player, PlayerCameraView view) noexcept { if (player == 0U || !view.camera.IsValid() || !view.target.IsValid()) return false; views_[player] = view; return true; }
const PlayerCameraView* CameraManager::FindView(PlayerId player) const noexcept { const auto it=views_.find(player); return it==views_.end()?nullptr:&it->second; }
bool CameraManager::ClearView(PlayerId player) noexcept { return views_.erase(player)!=0U; }
} // namespace kb::gameplay
