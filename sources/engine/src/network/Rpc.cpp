#include "engine/network/Rpc.hpp"

namespace kb::network {
bool ValidateRpc(const NetworkObjects& objects, const RpcDescriptor& rpc) noexcept { if(rpc.object==0U||rpc.sender==0U)return false; const auto object=objects.Find(rpc.object); if(!object.has_value())return false; return rpc.direction==RpcDirection::ClientToServer ? objects.CanAcceptOwnerCommand(rpc.object,rpc.sender) : object->role==NetworkRole::Proxy&&object->owner==rpc.sender; }
} // namespace kb::network
