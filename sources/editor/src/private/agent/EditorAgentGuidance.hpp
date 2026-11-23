#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {

// How big a request is, as far as its words say: one change, a feature, or a large job over several systems.
enum class EditorAgentRequestSize : std::uint8_t {
    Small,
    Feature,
    Large,
};

// The words that tell a request's size, in the editor's language: the systems a game is made of, and the phrases that mean
// a whole game. A word ending in * matches every word that starts so ("fizyk*"); a term with a space is looked for as a phrase;
// alternatives of one term are separated by | and count once.
struct EditorAgentSizeWords {
    std::vector<std::string> systems;
    std::vector<std::string> large;

    [[nodiscard]] static EditorAgentSizeWords FromLists(std::string_view systems, std::string_view large);
};

// Length, lines, the systems it names, the phrases for a whole game and the files attached decide: a short request about one
// thing is Small, a request over four systems or a page long is Large, anything between is a Feature.
[[nodiscard]] EditorAgentRequestSize EstimateAgentRequestSize(std::string_view request, std::size_t attachments, const EditorAgentSizeWords& words);

// The models of an agent from lightest to strongest (its ladder): the order the program lists them, best first, turned round,
// unless the user wrote their own order for it.
[[nodiscard]] std::vector<std::string> ModelLadder(const std::vector<std::string>& listed, const std::vector<std::string>& userOrder);
// The lightest model for a Small request, the strongest for a Large one, the one between for a Feature; empty without a ladder.
[[nodiscard]] std::string ModelForSize(const std::vector<std::string>& ladder, EditorAgentRequestSize size);
// One step up from `current` (an empty `current` is the agent's own default, which may be any); empty when it is already the
// strongest or there is no ladder.
[[nodiscard]] std::string StrongerModel(const std::vector<std::string>& ladder, const std::string& current);

enum class EditorAgentStuck : std::uint8_t {
    None,
    SameCallFailedTwice, // one tool call with the same arguments failed twice
    SameErrorThreeTimes, // one error message came back three times
    NoProgress,          // several steps in a row changed nothing and brought nothing new
    TooCostly,           // far more steps or tokens than a request of this size normally takes
    StepNotDone,         // a plan's step ended without its check passing (set by the window, not the guard)
};

// Watches one request while it runs. Every finished tool call is recorded; the first sign of a loop stays in Reason() until the
// guard is reset (the person decided what to do) so the window can stop the agent and ask.
class EditorAgentLoopGuard final {
public:
    void Reset(EditorAgentRequestSize size);
    // A call went out under the agent's id `id`, and its result came back.
    void Begin(const std::string& id, std::string callKey);
    void End(const std::string& id, bool failed, std::string_view result, bool projectChanged);
    // A call whose start and end are reported together.
    void Record(const std::string& callKey, bool failed, std::string_view result, bool projectChanged);
    // The tokens the request has made the model read so far.
    void AddTokens(std::uint64_t processed) noexcept;

    [[nodiscard]] EditorAgentStuck Reason() const noexcept { return reason_; }
    [[nodiscard]] int Steps() const noexcept { return steps_; }

private:
    void Trip(EditorAgentStuck reason) noexcept {
        if (reason_ == EditorAgentStuck::None) reason_ = reason;
    }

    EditorAgentStuck reason_ = EditorAgentStuck::None;
    // Until a request is started, the limits of a Feature.
    int stepLimit_ = 45;
    std::uint64_t tokenLimit_ = 5'000'000U;
    int steps_ = 0;
    int idleSteps_ = 0;
    // Per call: how it last failed and how many times in a row it failed that way.
    std::map<std::string, std::pair<std::string, int>> failedCalls_;
    std::map<std::string, int> errors_;
    std::set<std::size_t> seen_;
    std::map<std::string, std::string> open_;
};

// Whether a new request is about something else than the earlier ones: none of their longer words, cut to a stem, is in it.
[[nodiscard]] bool AgentTopicChanged(std::string_view earlierRequests, std::string_view draft);
// One line that carries a finished conversation over to a new one: what was asked and what was answered, each cut short.
[[nodiscard]] std::string AgentCarrySummary(std::string_view lastRequest, std::string_view lastAnswer);

} // namespace kb::editor
