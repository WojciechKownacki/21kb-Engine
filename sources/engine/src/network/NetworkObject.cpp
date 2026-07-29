#include "engine/network/NetworkObject.hpp"

namespace kb::network {
bool NetworkObjects::Spawn(NetworkObject object) { return object.id!=0U&&object.owner!=0U&&object.role!=NetworkRole::None&&objects_.emplace(object.id,object).second; }
bool NetworkObjects::Despawn(NetworkObjectId id) noexcept { return objects_.erase(id)!=0U; }
bool NetworkObjects::AssignOwner(NetworkObjectId id, NetworkPeerId owner) noexcept { const auto found=objects_.find(id); if(found==objects_.end()||owner==0U||found->second.role!=NetworkRole::Authority)return false; found->second.owner=owner; return true; }
std::optional<NetworkObject> NetworkObjects::Find(NetworkObjectId id) const noexcept { const auto found=objects_.find(id); return found==objects_.end()?std::nullopt:std::optional<NetworkObject>{found->second}; }
bool NetworkObjects::CanAcceptOwnerCommand(NetworkObjectId id, NetworkPeerId sender) const noexcept { const auto object=Find(id); return object.has_value()&&object->role==NetworkRole::Authority&&object->owner==sender; }
} // namespace kb::network
