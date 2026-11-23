#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// Where one step of a plan stands.
enum class EditorAgentStepProgress : std::uint8_t {
    Pending,
    Running,
    Done,
    Stuck, // even the strongest model did not get it done
};

// One small request of a plan, and how to tell it is done.
struct EditorAgentPlanStep {
    std::string text;
    std::string check;
    // The result can be seen in the scene, so the step is only done when the agent took a picture of it.
    bool visual = false;
    // A step the person switched off is skipped.
    bool enabled = true;
    EditorAgentStepProgress progress = EditorAgentStepProgress::Pending;
    // The model this step runs on when the person pressed "continue with a stronger model" for it; empty: the one they chose.
    std::string model;
};

// A big request as a strong model split it, run one step at a time by a light one.
struct EditorAgentPlan {
    enum class Phase : std::uint8_t {
        None,
        Writing, // the strong model is writing the steps
        Review,  // the list waits for the person to change it and run it
        Running,
    };
    Phase phase = Phase::None;
    // What the person asked, which every step is part of.
    std::string goal;
    std::vector<EditorAgentPlanStep> steps;
    int current = -1;
    // The step whose text is being edited in the request box, or -1.
    int editing = -1;
    // Where in the conversation's entries the running step began, so its answer and pictures can be told from earlier ones.
    std::size_t stepEntry = 0U;
};

// The request a strong model gets to write the steps, and the steps read back from its answer: lines
// "1. what to do || how to check it || visual|plain". Between 2 and 10 steps; a list that is longer has its tail joined to the
// last step, one that has less than two is no plan (empty).
[[nodiscard]] std::string AgentPlanRequest(std::string_view goal);
[[nodiscard]] std::vector<EditorAgentPlanStep> ParseAgentPlan(std::string_view answer);
// The request for one step, started in a conversation of its own: the whole job, what is done, this step and its check.
[[nodiscard]] std::string AgentStepRequest(const EditorAgentPlan& plan, std::size_t index);
// For the same step on a stronger model that continues the conversation of the first try.
[[nodiscard]] std::string AgentStepRetryRequest(const EditorAgentPlanStep& step);

enum class EditorAgentVerdict : std::uint8_t { Missing, Passed, Failed };
// The last "Check passed:" or "Check failed:" line of an answer.
[[nodiscard]] EditorAgentVerdict ReadAgentVerdict(std::string_view answer);

} // namespace kb::editor
