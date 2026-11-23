#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

// Matching the words of a request ("shiny gold armour with scratches") against names and descriptions, for the guides, the
// ready materials and the material nodes: the same words, the same endings ("glowing" and "glows" are "glow"), the same score.

// The words a text says, lower case, without filler words, with endings removed. `splitCamelCase` reads PartialDerivativeX as
// partial, derivative, x.
[[nodiscard]] std::vector<std::string> SearchWords(std::string_view text, bool splitCamelCase = false);

// How well one word of a request fits an entry: 4 when it is a word of the entry's name, 2 when it is one of its other words,
// 1 when it starts the same way (at least four letters), else 0.
[[nodiscard]] int SearchWordScore(const std::string& word, const std::vector<std::string>& nameWords, const std::vector<std::string>& otherWords);

// The score of a whole request against an entry.
[[nodiscard]] int SearchScore(const std::vector<std::string>& requestWords, const std::vector<std::string>& nameWords, const std::vector<std::string>& otherWords);

} // namespace kb::script
