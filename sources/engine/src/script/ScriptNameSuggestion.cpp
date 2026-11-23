#include "engine/script/ScriptNameSuggestion.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] char Fold(char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

// Near enough to be a slip of the keyboard rather than another name.
[[nodiscard]] bool IsNear(std::size_t distance, std::size_t length) noexcept {
    return distance <= 1U || (distance <= 2U && length >= 5U) || (distance <= 3U && length >= 12U);
}

template <typename Candidates>
[[nodiscard]] std::string Closest(std::string_view name, const Candidates& candidates, bool onlyNear) {
    std::string best;
    std::size_t bestDistance = static_cast<std::size_t>(-1);
    for (const auto& candidate : candidates) {
        const std::string_view text{ candidate };
        const std::size_t distance = ScriptNameDistance(name, text);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = std::string{ text };
        }
    }
    if (onlyNear && !best.empty() && !IsNear(bestDistance, std::max(name.size(), best.size()))) {
        return {};
    }
    return best;
}

} // namespace

std::size_t ScriptNameDistance(std::string_view first, std::string_view second) {
    std::vector<std::size_t> previous(second.size() + 1U);
    std::vector<std::size_t> current(second.size() + 1U);
    for (std::size_t column = 0U; column <= second.size(); ++column) previous[column] = column;
    for (std::size_t row = 1U; row <= first.size(); ++row) {
        current[0] = row;
        for (std::size_t column = 1U; column <= second.size(); ++column) {
            const std::size_t substitution = previous[column - 1U] + (Fold(first[row - 1U]) == Fold(second[column - 1U]) ? 0U : 1U);
            current[column] = std::min({ previous[column] + 1U, current[column - 1U] + 1U, substitution });
        }
        std::swap(previous, current);
    }
    return previous[second.size()];
}

std::string ClosestScriptName(std::string_view name, std::span<const std::string_view> candidates) {
    return Closest(name, candidates, true);
}

std::string ClosestScriptName(std::string_view name, std::span<const std::string> candidates) {
    return Closest(name, candidates, true);
}

std::string NearestScriptName(std::string_view name, std::span<const std::string_view> candidates) {
    return Closest(name, candidates, false);
}

} // namespace kb::script
