#include "engine/gameplay/GameplayModules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::gameplay {
namespace {
[[nodiscard]] bool IsValid(HealthState state) noexcept { return std::isfinite(state.current) && std::isfinite(state.maximum) && state.maximum > 0.0F && state.current >= 0.0F && state.current <= state.maximum; }
[[nodiscard]] bool IsValid(AttributeState state) noexcept { return std::isfinite(state.current) && std::isfinite(state.minimum) && std::isfinite(state.maximum) && state.minimum <= state.maximum && state.current >= state.minimum && state.current <= state.maximum; }
} // namespace

bool GameplayModules::AddHealth(kb::scene::SceneEntity entity, HealthState state) { return entity.IsValid() && IsValid(state) && health_.emplace(entity.Id(), state).second; }
std::optional<HealthState> GameplayModules::Health(kb::scene::SceneEntity entity) const noexcept { const auto found=health_.find(entity.Id()); return found==health_.end()?std::nullopt:std::optional<HealthState>{found->second}; }
bool GameplayModules::ApplyDamage(const DamageResolution& resolution) noexcept { if(!resolution.event.target.IsValid() || !std::isfinite(resolution.healthDelta))return false; const auto found=health_.find(resolution.event.target.Id()); if(found==health_.end())return false; const float next=std::clamp(found->second.current+resolution.healthDelta,0.0F,found->second.maximum); if(next==found->second.current)return false; found->second.current=next; return true; }
bool GameplayModules::SetAttribute(kb::scene::SceneEntity entity, AttributeId id, AttributeState state) { if(!entity.IsValid()||id==0U||!IsValid(state))return false; attributes_[entity.Id()].insert_or_assign(id,state); return true; }
std::optional<AttributeState> GameplayModules::Attribute(kb::scene::SceneEntity entity, AttributeId id) const noexcept { const auto entityFound=attributes_.find(entity.Id()); if(entityFound==attributes_.end())return std::nullopt; const auto attributeFound=entityFound->second.find(id); return attributeFound==entityFound->second.end()?std::nullopt:std::optional<AttributeState>{attributeFound->second}; }
bool GameplayModules::SpendAttribute(kb::scene::SceneEntity entity, AttributeId id, float amount) noexcept { if(!std::isfinite(amount)||amount<0.0F)return false; const auto entityFound=attributes_.find(entity.Id()); if(entityFound==attributes_.end())return false; const auto attributeFound=entityFound->second.find(id); if(attributeFound==entityFound->second.end()||attributeFound->second.current<amount)return false; attributeFound->second.current-=amount; return true; }
bool GameplayModules::AddItems(kb::scene::SceneEntity entity, GameplayItemId item, std::uint32_t quantity) { if(!entity.IsValid()||item==0U||quantity==0U)return false; auto& inventory=inventories_[entity.Id()]; const auto found=inventory.find(item); if(found!=inventory.end()&&found->second>std::numeric_limits<std::uint32_t>::max()-quantity)return false; if(found==inventory.end())inventory.emplace(item,quantity);else found->second+=quantity; return true; }
std::uint32_t GameplayModules::ItemCount(kb::scene::SceneEntity entity, GameplayItemId item) const noexcept { const auto entityFound=inventories_.find(entity.Id()); if(entityFound==inventories_.end())return 0U; const auto itemFound=entityFound->second.find(item); return itemFound==entityFound->second.end()?0U:itemFound->second; }
bool GameplayModules::Equip(kb::scene::SceneEntity entity, GameplayTagId slot, GameplayItemId item) { if(!entity.IsValid()||slot==0U||ItemCount(entity,item)==0U)return false; equipment_[entity.Id()].insert_or_assign(slot,item); return true; }
std::optional<GameplayItemId> GameplayModules::Equipped(kb::scene::SceneEntity entity, GameplayTagId slot) const noexcept { const auto entityFound=equipment_.find(entity.Id()); if(entityFound==equipment_.end())return std::nullopt; const auto itemFound=entityFound->second.find(slot); return itemFound==entityFound->second.end()?std::nullopt:std::optional<GameplayItemId>{itemFound->second}; }
bool GameplayModules::RegisterPickup(kb::scene::SceneEntity entity, PickupState pickup) { return entity.IsValid()&&pickup.item!=0U&&pickup.quantity!=0U&&pickups_.emplace(entity.Id(),pickup).second; }
bool GameplayModules::CollectPickup(kb::scene::SceneEntity pickup, kb::scene::SceneEntity collector) { const auto found=pickups_.find(pickup.Id()); if(!collector.IsValid()||found==pickups_.end()||!AddItems(collector,found->second.item,found->second.quantity))return false; pickups_.erase(found); return true; }
bool GameplayModules::Remove(kb::scene::SceneEntity entity) noexcept { const EntityId id=entity.Id(); return health_.erase(id)!=0U||attributes_.erase(id)!=0U||inventories_.erase(id)!=0U||equipment_.erase(id)!=0U||pickups_.erase(id)!=0U; }
} // namespace kb::gameplay
