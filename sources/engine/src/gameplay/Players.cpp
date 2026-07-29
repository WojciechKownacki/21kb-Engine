#include "engine/gameplay/Players.hpp"

namespace kb::gameplay {
bool Players::Join(Player player) { return player.id != 0U && !player.state.displayName.empty() && players_.emplace(player.id, std::move(player)).second; }
bool Players::Leave(PlayerId id) noexcept { return players_.erase(id) != 0U; }
Player* Players::Find(PlayerId id) noexcept { const auto it = players_.find(id); return it == players_.end() ? nullptr : &it->second; }
const Player* Players::Find(PlayerId id) const noexcept { return const_cast<Players*>(this)->Find(id); }
bool Players::Possess(PlayerId id, kb::scene::SceneEntity pawn) noexcept { Player* player = Find(id); if (player == nullptr) return false; player->controller.pawn = pawn; return true; }
std::uint32_t Players::Count() const noexcept { return static_cast<std::uint32_t>(players_.size()); }
} // namespace kb::gameplay
