#include "engine/script/ScriptAgentSearch.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace kb::script {
namespace {

constexpr std::array<std::string_view, 30> kFillerWords{ "a", "an", "the", "of", "for", "with", "and", "or", "to", "in", "on", "my", "me", "i", "it", "that", "this",
    "is", "are", "make", "want", "need", "some", "like", "when", "then", "look", "looks", "effect", "material" };

[[nodiscard]] std::string Stem(std::string word) {
    const auto ends = [&](std::string_view tail) { return word.size() > tail.size() + 2U && word.ends_with(tail); };
    if (ends("ies")) {
        word.resize(word.size() - 3U);
        word += 'y';
    } else if (ends("ing")) {
        word.resize(word.size() - 3U);
    } else if (ends("ed")) {
        word.resize(word.size() - 2U);
    } else if (ends("es") && word.size() > 4U) {
        word.resize(word.size() - 2U);
    } else if (ends("s")) {
        word.resize(word.size() - 1U);
    }
    return word;
}

[[nodiscard]] bool SharesStart(const std::string& left, const std::string& right) {
    return left.size() >= 4U && right.size() >= 4U && (left.starts_with(right) || right.starts_with(left));
}

} // namespace

std::vector<std::string> SearchWords(std::string_view text, bool splitCamelCase) {
    std::vector<std::string> words;
    std::string current;
    const auto flush = [&]() {
        if (current.size() >= 2U && std::ranges::find(kFillerWords, current) == kFillerWords.end()) words.push_back(Stem(current));
        current.clear();
    };
    char previous = ' ';
    for (const char character : text) {
        const bool alphanumeric = std::isalnum(static_cast<unsigned char>(character)) != 0;
        if (splitCamelCase && alphanumeric && std::isupper(static_cast<unsigned char>(character)) != 0 && std::islower(static_cast<unsigned char>(previous)) != 0) flush();
        if (alphanumeric) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        } else {
            flush();
        }
        previous = character;
    }
    flush();
    return words;
}

int SearchWordScore(const std::string& word, const std::vector<std::string>& nameWords, const std::vector<std::string>& otherWords) {
    int best = 0;
    for (const std::string& candidate : nameWords) {
        if (candidate == word) best = std::max(best, 4);
        else if (SharesStart(candidate, word)) best = std::max(best, 1);
    }
    for (const std::string& candidate : otherWords) {
        if (candidate == word) best = std::max(best, 2);
        else if (SharesStart(candidate, word)) best = std::max(best, 1);
    }
    return best;
}

int SearchScore(const std::vector<std::string>& requestWords, const std::vector<std::string>& nameWords, const std::vector<std::string>& otherWords) {
    int score = 0;
    for (const std::string& word : requestWords) score += SearchWordScore(word, nameWords, otherWords);
    return score;
}

} // namespace kb::script
