#pragma once

#include <array>
#include <cstddef>

namespace kb::ecs {

inline constexpr std::size_t kMaxQueryTerms = 32;
using QueryComponentPointerBlock = std::array<const void*, kMaxQueryTerms>;
using MutableQueryComponentPointerBlock = std::array<void*, kMaxQueryTerms>;

} // namespace kb::ecs
