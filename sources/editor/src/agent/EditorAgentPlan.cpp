#include "agent/EditorAgentPlan.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor {
namespace {

constexpr std::size_t kMostSteps = 10U;

[[nodiscard]] std::string Trimmed(std::string_view text) {
    std::size_t start = 0U;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) --end;
    return std::string{ text.substr(start, end - start) };
}

[[nodiscard]] std::string Lowered(std::string_view text) {
    std::string lowered{ text };
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

constexpr std::string_view kVerdictLine =
    "Finish with one line that starts with \"Check passed:\" or \"Check failed:\" and tells what you saw.";

} // namespace

std::string AgentPlanRequest(std::string_view goal) {
    return "Write a plan for the job below. Do not change anything and do not run a command that changes the project.\n"
           "Reply with only a numbered list of 3 to 10 steps, one step per line, in this form:\n"
           "1. <what to do, written as a normal request> || <how to check it is done> || <visual or plain>\n"
           "Each step must be small enough to be finished and checked on its own, and later steps may rely on earlier ones. "
           "Write the steps in the language of the job. Write visual when the result can be seen in the scene, otherwise plain.\n\n"
           "The job:\n" + std::string{ goal };
}

std::vector<EditorAgentPlanStep> ParseAgentPlan(std::string_view answer) {
    std::vector<EditorAgentPlanStep> steps;
    for (std::size_t start = 0U; start < answer.size();) {
        const std::size_t end = std::min(answer.find('\n', start), answer.size());
        std::string line = Trimmed(answer.substr(start, end - start));
        start = end + 1U;
        std::size_t at = 0U;
        while (at < line.size() && std::isdigit(static_cast<unsigned char>(line[at])) != 0) ++at;
        if (at == 0U || at >= line.size() || (line[at] != '.' && line[at] != ')')) continue;
        line = Trimmed(std::string_view{ line }.substr(at + 1U));
        // "what || check || visual", the parts in that order.
        std::vector<std::string> parts;
        for (std::size_t from = 0U;;) {
            const std::size_t bar = line.find("||", from);
            parts.push_back(Trimmed(std::string_view{ line }.substr(from, bar == std::string::npos ? std::string::npos : bar - from)));
            if (bar == std::string::npos) break;
            from = bar + 2U;
        }
        // The answer may carry markdown emphasis around a step.
        std::erase(parts[0], '*');
        if (parts[0].empty()) continue;
        EditorAgentPlanStep step;
        step.text = parts[0];
        if (parts.size() > 1U) step.check = parts[1];
        step.visual = parts.size() > 2U && Lowered(parts[2]).find("visual") != std::string::npos;
        if (steps.size() == kMostSteps) {
            steps.back().text += "; " + step.text;
            continue;
        }
        steps.push_back(std::move(step));
    }
    if (steps.size() < 2U) steps.clear();
    return steps;
}

std::string AgentStepRequest(const EditorAgentPlan& plan, std::size_t index) {
    const EditorAgentPlanStep& step = plan.steps[index];
    std::string request = "You do one step of a larger job.\nThe whole job: " + plan.goal + "\n";
    std::string done;
    for (std::size_t before = 0U; before < index; ++before) {
        if (plan.steps[before].progress == EditorAgentStepProgress::Done) done += "- " + plan.steps[before].text + "\n";
    }
    if (!done.empty()) request += "Already done:\n" + done;
    request += "This step: " + step.text + "\n";
    if (!step.check.empty()) request += "Done when: " + step.check + "\n";
    if (step.visual) request += "The result can be seen in the scene: take a picture with kb_cli capture and look at it before you decide.\n";
    request += "Do only this step. " + std::string{ kVerdictLine };
    return request;
}

std::string AgentStepRetryRequest(const EditorAgentPlanStep& step) {
    std::string request = "The earlier attempt at this step did not work out. Look at what is in the project now, finish the step and check it again.\n"
                          "This step: " + step.text + "\n";
    if (!step.check.empty()) request += "Done when: " + step.check + "\n";
    if (step.visual) request += "Take a picture with kb_cli capture and look at it before you decide.\n";
    request += std::string{ kVerdictLine };
    return request;
}

EditorAgentVerdict ReadAgentVerdict(std::string_view answer) {
    const std::string lowered = Lowered(answer);
    const std::size_t passed = lowered.rfind("check passed");
    const std::size_t failed = lowered.rfind("check failed");
    if (passed == std::string::npos && failed == std::string::npos) return EditorAgentVerdict::Missing;
    if (failed == std::string::npos || (passed != std::string::npos && passed > failed)) return EditorAgentVerdict::Passed;
    return EditorAgentVerdict::Failed;
}

} // namespace kb::editor
