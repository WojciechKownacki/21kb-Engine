#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace kb::script {

// How far apart two names are, ignoring letter case: the number of single-letter insertions, deletions and
// substitutions that turn one into the other.
[[nodiscard]] std::size_t ScriptNameDistance(std::string_view first, std::string_view second);

// The candidate nearest to `name`, or an empty string when none is near enough to be what was meant. A name that
// differs only in case, or by a letter or two in a longer name, is near; the first of equally near candidates wins.
[[nodiscard]] std::string ClosestScriptName(std::string_view name, std::span<const std::string_view> candidates);
[[nodiscard]] std::string ClosestScriptName(std::string_view name, std::span<const std::string> candidates);

// Like ClosestScriptName, but always names the nearest candidate however far it is (empty only without candidates).
[[nodiscard]] std::string NearestScriptName(std::string_view name, std::span<const std::string_view> candidates);

} // namespace kb::script
