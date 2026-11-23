#include "agent/EditorAgentGuidance.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace kb::editor {
namespace {

// What a request of each size normally takes: steps, and tokens the model reads over all of them (every step reads the whole
// conversation again). Far beyond it, the agent is looping or lost.
struct Norm {
    int steps;
    std::uint64_t tokens;
};
constexpr Norm kNorms[]{ { 20, 2'000'000U }, { 45, 5'000'000U }, { 100, 12'000'000U } };
// Steps in a row that changed nothing and brought nothing new.
constexpr int kIdleLimit = 6;

[[nodiscard]] std::string Lowered(std::string_view text) {
    std::string lowered{ text };
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

// Letters and digits; any byte of a non-ASCII character counts, so words with accents hold together.
[[nodiscard]] bool WordByte(unsigned char c) noexcept {
    return c >= 0x80U || std::isalnum(c) != 0 || c == '_';
}

[[nodiscard]] std::vector<std::string> Words(std::string_view lowered) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : lowered) {
        if (WordByte(static_cast<unsigned char>(c))) {
            word.push_back(c);
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

[[nodiscard]] std::vector<std::string> Terms(std::string_view list) {
    std::vector<std::string> terms;
    std::size_t start = 0U;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(',', start), list.size());
        std::string term = Lowered(list.substr(start, end - start));
        while (!term.empty() && term.front() == ' ') term.erase(term.begin());
        while (!term.empty() && term.back() == ' ') term.pop_back();
        if (!term.empty()) terms.push_back(std::move(term));
        start = end + 1U;
    }
    return terms;
}

[[nodiscard]] bool MatchesOne(const std::string& term, const std::vector<std::string>& words, const std::string& lowered) {
    if (term.find(' ') != std::string::npos) return lowered.find(term) != std::string::npos;
    if (term.back() == '*') {
        const std::string_view stem{ term.data(), term.size() - 1U };
        return std::ranges::any_of(words, [stem](const std::string& word) { return word.starts_with(stem); });
    }
    return std::ranges::find(words, term) != words.end();
}

// A term is one or more alternatives separated by |, which count as one system however many of them the request names.
[[nodiscard]] bool Matches(const std::string& group, const std::vector<std::string>& words, const std::string& lowered) {
    for (std::size_t start = 0U; start <= group.size();) {
        const std::size_t end = std::min(group.find('|', start), group.size());
        std::string alternative = group.substr(start, end - start);
        while (!alternative.empty() && alternative.back() == ' ') alternative.pop_back();
        while (!alternative.empty() && alternative.front() == ' ') alternative.erase(alternative.begin());
        if (!alternative.empty() && MatchesOne(alternative, words, lowered)) return true;
        start = end + 1U;
    }
    return false;
}

// Files named in the text: a quoted word with a dot in it, as the attach button writes a path.
[[nodiscard]] std::size_t QuotedFiles(std::string_view text) {
    std::size_t count = 0U;
    for (std::size_t open = text.find('"'); open != std::string_view::npos;) {
        const std::size_t close = text.find('"', open + 1U);
        if (close == std::string_view::npos) break;
        const std::string_view inside = text.substr(open + 1U, close - open - 1U);
        if (inside.find('.') != std::string_view::npos && inside.find(' ') == std::string_view::npos && inside.size() < 260U) ++count;
        open = text.find('"', close + 1U);
    }
    return count;
}

// The text cut to `limit` bytes at a character boundary, with "..." when something was left out.
[[nodiscard]] std::string Cut(std::string_view text, std::size_t limit) {
    std::string flat;
    for (const char c : text) flat.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
    while (!flat.empty() && flat.front() == ' ') flat.erase(flat.begin());
    while (!flat.empty() && flat.back() == ' ') flat.pop_back();
    if (flat.size() <= limit) return flat;
    std::size_t at = limit;
    while (at > 0U && (static_cast<unsigned char>(flat[at]) & 0xC0U) == 0x80U) --at;
    return flat.substr(0U, at) + "...";
}

// An error message as the same error is told twice: lower case, numbers as '#', spaces folded, only its start.
[[nodiscard]] std::string Normal(std::string_view text) {
    std::string out;
    bool space = false;
    bool number = false;
    for (const char raw : text.substr(0U, 200U)) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isdigit(c) != 0) {
            if (!number) out.push_back('#');
            number = true;
            space = false;
            continue;
        }
        number = false;
        if (std::isspace(c) != 0) {
            if (!space && !out.empty()) out.push_back(' ');
            space = true;
            continue;
        }
        space = false;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

[[nodiscard]] std::vector<std::string> Stems(std::string_view text) {
    std::vector<std::string> stems;
    for (std::string& word : Words(Lowered(text))) {
        if (word.size() < 4U) continue;
        word.resize(std::min<std::size_t>(word.size(), 5U));
        if (std::ranges::find(stems, word) == stems.end()) stems.push_back(std::move(word));
    }
    return stems;
}

} // namespace

EditorAgentSizeWords EditorAgentSizeWords::FromLists(std::string_view systems, std::string_view large) {
    return { Terms(systems), Terms(large) };
}

EditorAgentRequestSize EstimateAgentRequestSize(std::string_view request, std::size_t attachments, const EditorAgentSizeWords& words) {
    const std::string lowered = Lowered(request);
    const std::vector<std::string> tokens = Words(lowered);
    std::size_t systems = 0U;
    for (const std::string& term : words.systems) {
        if (Matches(term, tokens, lowered)) ++systems;
    }
    const bool wholeGame = std::ranges::any_of(words.large, [&](const std::string& term) { return Matches(term, tokens, lowered); });
    std::size_t lines = 0U;
    for (std::size_t start = 0U; start < request.size();) {
        const std::size_t end = std::min(request.find('\n', start), request.size());
        if (request.substr(start, end - start).find_first_not_of(" \t\r") != std::string_view::npos) ++lines;
        start = end + 1U;
    }
    const std::size_t files = attachments + QuotedFiles(request);
    if (wholeGame || systems >= 4U || request.size() >= 1500U || lines >= 8U) return EditorAgentRequestSize::Large;
    if (request.size() <= 160U && systems <= 1U && lines <= 1U && files <= 1U) return EditorAgentRequestSize::Small;
    return EditorAgentRequestSize::Feature;
}

std::vector<std::string> ModelLadder(const std::vector<std::string>& listed, const std::vector<std::string>& userOrder) {
    if (!userOrder.empty()) return userOrder;
    return { listed.rbegin(), listed.rend() };
}

std::string ModelForSize(const std::vector<std::string>& ladder, EditorAgentRequestSize size) {
    if (ladder.empty()) return {};
    switch (size) {
    case EditorAgentRequestSize::Small: return ladder.front();
    case EditorAgentRequestSize::Large: return ladder.back();
    case EditorAgentRequestSize::Feature: break;
    }
    return ladder[ladder.size() / 2U];
}

std::string StrongerModel(const std::vector<std::string>& ladder, const std::string& current) {
    if (ladder.empty()) return {};
    // The default may be any rung: only the top is certainly above it.
    const auto at = current.empty() ? ladder.end() : std::ranges::find(ladder, current);
    if (at == ladder.end()) return ladder.back();
    return at + 1 == ladder.end() ? std::string{} : *(at + 1);
}

void EditorAgentLoopGuard::Reset(EditorAgentRequestSize size) {
    const Norm& norm = kNorms[static_cast<std::size_t>(size)];
    *this = {};
    stepLimit_ = norm.steps;
    tokenLimit_ = norm.tokens;
}

void EditorAgentLoopGuard::Begin(const std::string& id, std::string callKey) {
    open_[id] = std::move(callKey);
}

void EditorAgentLoopGuard::End(const std::string& id, bool failed, std::string_view result, bool projectChanged) {
    const auto found = open_.find(id);
    const std::string key = found != open_.end() ? found->second : id;
    if (found != open_.end()) open_.erase(found);
    Record(key, failed, result, projectChanged);
}

void EditorAgentLoopGuard::Record(const std::string& callKey, bool failed, std::string_view result, bool projectChanged) {
    ++steps_;
    bool news = projectChanged;
    if (failed) {
        // The same call is a loop only when it fails the same way: a run that fails on another error after a fix is work going on.
        const std::string error = Normal(result);
        auto& previous = failedCalls_[callKey];
        previous = previous.second > 0 && previous.first == error ? std::pair{ error, previous.second + 1 } : std::pair{ error, 1 };
        if (previous.second >= 2) Trip(EditorAgentStuck::SameCallFailedTwice);
        if (++errors_[error] >= 3) Trip(EditorAgentStuck::SameErrorThreeTimes);
    } else {
        failedCalls_.erase(callKey);
        // A result the agent has not read before is something new, even when nothing in the project changed.
        news = news || (!result.empty() && seen_.insert(std::hash<std::string_view>{}(result)).second);
    }
    idleSteps_ = news ? 0 : idleSteps_ + 1;
    if (idleSteps_ >= kIdleLimit) Trip(EditorAgentStuck::NoProgress);
    if (steps_ > stepLimit_) Trip(EditorAgentStuck::TooCostly);
}

void EditorAgentLoopGuard::AddTokens(std::uint64_t processed) noexcept {
    if (tokenLimit_ != 0U && processed > tokenLimit_) Trip(EditorAgentStuck::TooCostly);
}

bool AgentTopicChanged(std::string_view earlierRequests, std::string_view draft) {
    const std::vector<std::string> now = Stems(draft);
    const std::vector<std::string> before = Stems(earlierRequests);
    // Too little said to judge.
    if (now.size() < 4U || before.size() < 3U) return false;
    return std::ranges::none_of(now, [&before](const std::string& stem) { return std::ranges::find(before, stem) != before.end(); });
}

std::string AgentCarrySummary(std::string_view lastRequest, std::string_view lastAnswer) {
    std::string summary = "\"" + Cut(lastRequest, 90U) + "\"";
    if (const std::string answer = Cut(lastAnswer.substr(0U, std::min(lastAnswer.find('\n'), lastAnswer.size())), 90U); !answer.empty()) {
        summary += " -> \"" + answer + "\"";
    }
    return summary;
}

} // namespace kb::editor
