#pragma once

#include "engine/scene/TransformComponent.hpp"
#include <array>

namespace exact_local_batch {

// Private batch-writer helper. False performs no FP
// arithmetic and leaves output unchanged; the engine must use its scalar writer.
// Input/output may be the same array. Four objects, no alignment assumption.
[[nodiscard]] bool TryNormalizeRoot4Exact(const std::array<kb::scene::Quat, 4U>& input,
    std::array<kb::scene::Quat, 4U>& output) noexcept;

// Reads four real rotation subobjects from the caller's retained TRS snapshot.
// Position/scale are untouched. False leaves output unchanged without FP arithmetic.
[[nodiscard]] bool TryNormalizeRoot4Exact(const std::array<kb::scene::LocalTransform, 4U>& input,
    std::array<kb::scene::Quat, 4U>& output) noexcept;

} // namespace exact_local_batch
