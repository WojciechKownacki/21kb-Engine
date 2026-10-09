#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <cstdint>

namespace cpu_reference {

struct Agent {
    float heading = 0.0F;
    float speed = 0.0F;
    std::uint32_t index = 0U;
};

struct Row {
    Agent agent;
    kb::scene::TransformComponent transform;
};

struct Lcg {
    std::uint32_t state = 12345U;
    float Next01();
};

// Implementations are deliberately in a separate /GL- translation unit. Candidate callbacks and engine
// libraries may change optimization modes without letting the independent scalar oracle change with them.
Row MakeInitialRow(Lcg& random, std::size_t index);
kb::scene::Quat NormalizeRoot(kb::scene::Quat value);
void ComposeRoot(kb::scene::TransformComponent& transform);
std::size_t AdvanceRow(Row& expected, std::uint64_t frame, float dt);

} // namespace cpu_reference
