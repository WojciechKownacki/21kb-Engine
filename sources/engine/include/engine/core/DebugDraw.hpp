#pragma once
#include "engine/math/EngineMath.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
namespace kb::core { enum class DebugDrawKind { Line, Ray, Box, Sphere, Text }; struct DebugDrawCommand { DebugDrawKind kind{}; kb::math::Vec3 a{};kb::math::Vec3 b{};float duration=0.0F;std::uint32_t channel=0U;}; class DebugDrawBuffer final {public:explicit DebugDrawBuffer(std::size_t capacity):capacity_(capacity){} [[nodiscard]] bool Add(DebugDrawCommand command){if(commands_.size()==capacity_)return false;commands_.push_back(command);return true;}void Advance(float delta){commands_.erase(std::remove_if(commands_.begin(),commands_.end(),[delta](auto& c){c.duration-=delta;return c.duration<=0.0F;}),commands_.end());}private:std::size_t capacity_;std::vector<DebugDrawCommand> commands_;}; }
