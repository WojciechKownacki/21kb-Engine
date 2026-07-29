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
[[nodiscard]] inline std::optional<NetworkSnapshot> Interpolate(const InterpolationWindow& window, float alpha) noexcept { if(!IsValidSnapshot(window.previous)||!IsValidSnapshot(window.next)||window.previous.tick>window.next.tick||!std::isfinite(alpha)||alpha<0.0F||alpha>1.0F)return std::nullopt; NetworkSnapshot result=window.next; result.position={window.previous.position.x+(window.next.position.x-window.previous.position.x)*alpha,window.previous.position.y+(window.next.position.y-window.previous.position.y)*alpha,window.previous.position.z+(window.next.position.z-window.previous.position.z)*alpha}; result.velocity={window.previous.velocity.x+(window.next.velocity.x-window.previous.velocity.x)*alpha,window.previous.velocity.y+(window.next.velocity.y-window.previous.velocity.y)*alpha,window.previous.velocity.z+(window.next.velocity.z-window.previous.velocity.z)*alpha}; return result; }
[[nodiscard]] inline bool RequiresReconciliation(const NetworkSnapshot& predicted, const NetworkSnapshot& authoritative, float tolerance) noexcept { if(!IsValidSnapshot(predicted)||!IsValidSnapshot(authoritative)||!std::isfinite(tolerance)||tolerance<0.0F)return true; const float dx=predicted.position.x-authoritative.position.x; const float dy=predicted.position.y-authoritative.position.y; const float dz=predicted.position.z-authoritative.position.z; return dx*dx+dy*dy+dz*dz>tolerance*tolerance; }

} // namespace kb::network
