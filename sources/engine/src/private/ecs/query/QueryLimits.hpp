#pragma once

#include <flecs.h>

#include <array>
#include <cstddef>

namespace kb::ecs {

inline constexpr std::size_t kMaxQueryTerms = FLECS_TERM_COUNT_MAX;
using QueryComponentPointerBlock = std::array<const void*, kMaxQueryTerms>;

} // namespace kb::ecs
