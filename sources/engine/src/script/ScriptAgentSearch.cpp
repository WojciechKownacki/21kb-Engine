#include "engine/script/ScriptAgentSearch.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace kb::script {
namespace {

constexpr auto kFillerWords = std::to_array<std::string_view>({ "a", "an", "the", "of", "for", "with", "and", "or", "to", "in", "on", "my", "me", "i", "it", "that", "this",
    "is", "are", "make", "want", "need", "some", "like", "when", "then", "look", "looks", "effect", "material",
    // Polish: what the customer says round the thing they mean.
    "gra", "gry", "gre", "chce", "chcialbym", "zrob", "zrobic", "mam", "ma", "po", "ktory", "ktora", "ktore", "moja", "moj", "moje", "typu", "jak", "sie", "na", "do", "ze",
    "dla", "czy", "zeby", "ale", "game", "games",
    // English function words that say nothing about the job.
    "can", "could", "should", "would", "will", "be", "been", "have", "has", "get", "where", "what", "how", "which", "who", "does", "if", "by", "at", "as", "from", "you", "your", "we", "they" });

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

// Whether two words start the same way: one begins the other, or they differ only in their last letters (Polish endings change a
// word after its stem: platformowa and platformowej, kamera and kamery).
[[nodiscard]] bool SharesStart(const std::string& left, const std::string& right) {
    if (left.size() < 4U || right.size() < 4U) return false;
    if (left.starts_with(right) || right.starts_with(left)) return true;
    std::size_t common = 0U;
    while (common < left.size() && common < right.size() && left[common] == right[common]) ++common;
    return common >= 5U && common + 2U >= std::min(left.size(), right.size());
}

// The plain letter of a Polish letter written in UTF-8 (two bytes), so a customer who types "gora" or "góra" finds the
// same words; 0 for any other pair of bytes.
[[nodiscard]] char PlainPolishLetter(unsigned char lead, unsigned char next) noexcept {
    struct Letter {
        unsigned char lead;
        unsigned char next;
        char plain;
    };
    constexpr std::array<Letter, 18> kLetters{ {
        { 0xC4, 0x85, 'a' }, { 0xC4, 0x84, 'a' }, { 0xC4, 0x87, 'c' }, { 0xC4, 0x86, 'c' }, { 0xC4, 0x99, 'e' }, { 0xC4, 0x98, 'e' },
        { 0xC5, 0x82, 'l' }, { 0xC5, 0x81, 'l' }, { 0xC5, 0x84, 'n' }, { 0xC5, 0x83, 'n' }, { 0xC3, 0xB3, 'o' }, { 0xC3, 0x93, 'o' },
        { 0xC5, 0x9B, 's' }, { 0xC5, 0x9A, 's' }, { 0xC5, 0xBA, 'z' }, { 0xC5, 0xB9, 'z' }, { 0xC5, 0xBC, 'z' }, { 0xC5, 0xBB, 'z' },
    } };
    for (const Letter& letter : kLetters) {
        if (letter.lead == lead && letter.next == next) return letter.plain;
    }
    return 0;
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
    for (std::size_t index = 0U; index < text.size(); ++index) {
        char character = text[index];
        if (index + 1U < text.size()) {
            if (const char plain = PlainPolishLetter(static_cast<unsigned char>(character), static_cast<unsigned char>(text[index + 1U])); plain != 0) {
                character = plain;
                ++index;
            }
        }
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
