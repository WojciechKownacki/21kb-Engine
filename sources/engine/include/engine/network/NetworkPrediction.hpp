#pragma once

#include "engine/math/EngineMath.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace kb::network {

struct InputCommand { std::uint64_t tick = 0U; std::uint32_t sequence = 0U; float moveX = 0.0F; float moveY = 0.0F; bool jump = false; };
struct NetworkSnapshot { std::uint64_t tick = 0U; std::uint32_t acknowledgedInput = 0U; kb::math::Vec3 position{}; kb::math::Vec3 velocity{}; };
struct InterpolationWindow { NetworkSnapshot previous{}; NetworkSnapshot next{}; };

[[nodiscard]] inline bool IsValidInputCommand(const InputCommand& command) noexcept { return command.tick!=0U&&std::isfinite(command.moveX)&&std::isfinite(command.moveY)&&command.moveX>=-1.0F&&command.moveX<=1.0F&&command.moveY>=-1.0F&&command.moveY<=1.0F; }
[[nodiscard]] inline bool IsValidSnapshot(const NetworkSnapshot& snapshot) noexcept { return snapshot.tick!=0U&&std::isfinite(snapshot.position.x)&&std::isfinite(snapshot.position.y)&&std::isfinite(snapshot.position.z)&&std::isfinite(snapshot.velocity.x)&&std::isfinite(snapshot.velocity.y)&&std::isfinite(snapshot.velocity.z); }
[[nodiscard]] inline std::optional<NetworkSnapshot> Interpolate(const InterpolationWindow& window, float alpha) noexcept { if(!IsValidSnapshot(window.previous)||!IsValidSnapshot(window.next)||window.previous.tick>=window.next.tick||!std::isfinite(alpha)||alpha<0.0F||alpha>1.0F)return std::nullopt; NetworkSnapshot result=window.next; result.position={window.previous.position.x+(window.next.position.x-window.previous.position.x)*alpha,window.previous.position.y+(window.next.position.y-window.previous.position.y)*alpha,window.previous.position.z+(window.next.position.z-window.previous.position.z)*alpha}; result.velocity={window.previous.velocity.x+(window.next.velocity.x-window.previous.velocity.x)*alpha,window.previous.velocity.y+(window.next.velocity.y-window.previous.velocity.y)*alpha,window.previous.velocity.z+(window.next.velocity.z-window.previous.velocity.z)*alpha}; return result; }
[[nodiscard]] inline bool RequiresReconciliation(const NetworkSnapshot& predicted, const NetworkSnapshot& authoritative, float tolerance) noexcept { if(!IsValidSnapshot(predicted)||!IsValidSnapshot(authoritative)||!std::isfinite(tolerance)||tolerance<0.0F||predicted.tick!=authoritative.tick||predicted.acknowledgedInput!=authoritative.acknowledgedInput)return true; const float positionX=predicted.position.x-authoritative.position.x; const float positionY=predicted.position.y-authoritative.position.y; const float positionZ=predicted.position.z-authoritative.position.z; const float velocityX=predicted.velocity.x-authoritative.velocity.x; const float velocityY=predicted.velocity.y-authoritative.velocity.y; const float velocityZ=predicted.velocity.z-authoritative.velocity.z; const float toleranceSquared=tolerance*tolerance; return positionX*positionX+positionY*positionY+positionZ*positionZ>toleranceSquared||velocityX*velocityX+velocityY*velocityY+velocityZ*velocityZ>toleranceSquared; }

} // namespace kb::network
