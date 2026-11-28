#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "agent/EditorAgentCatalog.hpp"
#include "agent/EditorAgentGuidance.hpp"
#include "agent/EditorAgentPlan.hpp"
#include "agent/EditorAgentPrices.hpp"
#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using kb::editor::EditorAgentLoopGuard;
using kb::editor::EditorAgentRequestSize;
using kb::editor::EditorAgentStuck;
using kb::editor::tests::Require;

void RunLoopGuardTests() {
    EditorAgentLoopGuard guard;
    guard.Reset(EditorAgentRequestSize::Feature);

    // The same call with the same arguments failing twice; a success in between starts the count again.
    guard.Record("call-a", true, "it broke", false);
    guard.Record("call-a", false, "fine now", false);
    guard.Record("call-a", true, "it broke", false);
    Require(guard.Reason() == EditorAgentStuck::None, "a success between two failures of one call is no loop");
    guard.Record("call-a", true, "it broke", false);
    Require(guard.Reason() == EditorAgentStuck::SameCallFailedTwice, "the same call failing twice in a row is a loop");

    // The same call failing on a different error each time is work going on (a fix between runs), not a loop.
    guard.Reset(EditorAgentRequestSize::Feature);
    guard.Record("run", true, "script compile failed: unknown name 'speed'", false);
    guard.Record("run", true, "script compile failed: missing ';' after the call", false);
    Require(guard.Reason() == EditorAgentStuck::None, "one call failing on two different errors is no loop");
    guard.Record("run", true, "script compile failed: missing ';' after the call", false);
    Require(guard.Reason() == EditorAgentStuck::SameCallFailedTwice, "the same call failing the same way twice in a row is a loop");

    // The same error message three times, however the calls differ; numbers in the message do not make it a different one.
    guard.Reset(EditorAgentRequestSize::Feature);
    guard.Record("call-1", true, "no entity named 'Crate 12'", false);
    guard.Record("call-2", true, "no entity named 'Crate 7'", false);
    Require(guard.Reason() == EditorAgentStuck::None, "two like errors are not yet a loop");
    guard.Record("call-3", true, "No entity named 'Crate 31'", false);
    Require(guard.Reason() == EditorAgentStuck::SameErrorThreeTimes, "the same error three times is a loop");

    // Steps that change nothing and tell nothing new; any change of the project, or any new answer, starts the count again.
    guard.Reset(EditorAgentRequestSize::Feature);
    guard.Record("look", false, "the same answer", false);
    for (int step = 0; step < 5; ++step) guard.Record("look", false, "the same answer", false);
    Require(guard.Reason() == EditorAgentStuck::None, "five steps in a row without a change or news are not yet a loop");
    guard.Record("look", false, "the same answer", false);
    Require(guard.Reason() == EditorAgentStuck::NoProgress, "six steps in a row without a change or news are a loop");
    guard.Reset(EditorAgentRequestSize::Feature);
    for (int step = 0; step < 30; ++step) guard.Record("look", false, "answer " + std::to_string(step), false);
    Require(guard.Reason() == EditorAgentStuck::None, "steps that each tell something new are not a loop");
    guard.Reset(EditorAgentRequestSize::Feature);
    for (int step = 0; step < 30; ++step) guard.Record("edit", false, "the same answer", step % 3 == 0);
    Require(guard.Reason() == EditorAgentStuck::None, "steps that change the project are not a loop");

    // A call told by its id.
    guard.Reset(EditorAgentRequestSize::Feature);
    guard.Begin("id-1", "call-b");
    guard.End("id-1", true, "bad", false);
    guard.Begin("id-2", "call-b");
    guard.End("id-2", true, "bad", false);
    Require(guard.Reason() == EditorAgentStuck::SameCallFailedTwice, "a call begun and ended by its id is recorded under its key");

    // Far more steps or tokens than a request of its size takes: a small one stops sooner than a large one.
    guard.Reset(EditorAgentRequestSize::Small);
    for (int step = 0; step < 21; ++step) guard.Record("edit", false, "result " + std::to_string(step), true);
    Require(guard.Reason() == EditorAgentStuck::TooCostly, "a small request over its steps is stopped");
    guard.Reset(EditorAgentRequestSize::Large);
    for (int step = 0; step < 21; ++step) guard.Record("edit", false, "result " + std::to_string(step), true);
    Require(guard.Reason() == EditorAgentStuck::None, "a large request may take the same number of steps");
    guard.Reset(EditorAgentRequestSize::Small);
    guard.AddTokens(1'000'000U);
    Require(guard.Reason() == EditorAgentStuck::None, "tokens within the norm do not stop a request");
    guard.AddTokens(3'000'000U);
    Require(guard.Reason() == EditorAgentStuck::TooCostly, "far more tokens than the norm stops a small request");
    guard.Reset(EditorAgentRequestSize::Large);
    Require(guard.Reason() == EditorAgentStuck::None && guard.Steps() == 0, "a reset forgets what was counted");
}

void RunSizeAndTierTests() {
    const auto words = kb::editor::EditorAgentSizeWords::FromLists(
        "physics|collision, ui|menu|hud, enemy|enemies|ai, terrain, light*|shadow*, audio|sound*", "whole game, multiplayer");
    using kb::editor::EstimateAgentRequestSize;
    Require(EstimateAgentRequestSize("Make the cube red", 0U, words) == EditorAgentRequestSize::Small, "one short change is small");
    Require(EstimateAgentRequestSize("Add a crate that falls with physics", 0U, words) == EditorAgentRequestSize::Small, "one system is still small");
    Require(EstimateAgentRequestSize("Add a menu with a start button and a hud that shows the score", 0U, words) == EditorAgentRequestSize::Small,
        "alternatives of one system count once");
    Require(EstimateAgentRequestSize("Add an enemy that chases the player and plays a sound when it hits", 0U, words) == EditorAgentRequestSize::Feature,
        "two systems make a feature");
    Require(EstimateAgentRequestSize("Make the cube red", 2U, words) == EditorAgentRequestSize::Feature, "two pictures make a feature");
    Require(EstimateAgentRequestSize("Make the cube red like \"Assets/Textures/a.png\" and \"Assets/Textures/b.png\"", 0U, words) == EditorAgentRequestSize::Feature,
        "two files named in the text make a feature");
    Require(EstimateAgentRequestSize("Build the whole game for me", 0U, words) == EditorAgentRequestSize::Large, "a phrase for a whole game is large");
    Require(EstimateAgentRequestSize("physics, a menu, enemies, terrain and shadows", 0U, words) == EditorAgentRequestSize::Large, "four systems are large");
    Require(EstimateAgentRequestSize("a\nb\nc\nd\ne\nf\ng\nh", 0U, words) == EditorAgentRequestSize::Large, "a long list is large");
    Require(EstimateAgentRequestSize(std::string(1600U, 'x'), 0U, words) == EditorAgentRequestSize::Large, "a page of text is large");
    // A word ending in * matches what starts so (the editor's other languages inflect their words).
    Require(EstimateAgentRequestSize("shadows and lighting", 0U, words) == EditorAgentRequestSize::Small, "light* and shadow* are one system");

    // The ladder: the program lists its models best first, the window works from the lightest.
    const std::vector<std::string> listed{ "strong", "middle", "light" };
    const std::vector<std::string> ladder = kb::editor::ModelLadder(listed, {});
    Require(ladder == std::vector<std::string>({ "light", "middle", "strong" }), "the ladder runs from the lightest model up");
    Require(kb::editor::ModelLadder(listed, { "x", "y" }) == std::vector<std::string>({ "x", "y" }), "the user's own order replaces the program's");
    Require(kb::editor::ModelForSize(ladder, EditorAgentRequestSize::Small) == "light", "a single change takes the lightest model");
    Require(kb::editor::ModelForSize(ladder, EditorAgentRequestSize::Feature) == "middle", "a feature takes the middle one");
    Require(kb::editor::ModelForSize(ladder, EditorAgentRequestSize::Large) == "strong", "a large request takes the strongest");
    Require(kb::editor::ModelForSize({}, EditorAgentRequestSize::Large).empty(), "without a ladder nothing is suggested");
    Require(kb::editor::StrongerModel(ladder, "light") == "middle" && kb::editor::StrongerModel(ladder, "middle") == "strong", "one step up the ladder");
    Require(kb::editor::StrongerModel(ladder, "strong").empty(), "the strongest has nothing above it");
    Require(kb::editor::StrongerModel(ladder, "").empty() == false && kb::editor::StrongerModel(ladder, "") == "strong",
        "the agent's own default may be any rung, so only the top is certainly above it");
    Require(kb::editor::StrongerModel(ladder, "unknown") == "strong", "a model not on the ladder is moved to the top");

    // A request about something else than the earlier ones, and the one line that carries a conversation over.
    Require(kb::editor::AgentTopicChanged("Make the crate red\nand the crate bigger", "Compose a short melody for the main menu please"),
        "a request that shares no word with the earlier ones is a new topic");
    Require(!kb::editor::AgentTopicChanged("Make the crate red\nand the crate bigger", "Now put the crate on the high shelf near the window please"),
        "a request that shares a word is the same topic");
    Require(!kb::editor::AgentTopicChanged("Make the crate red", "Make it bigger"), "too little said to judge is not a new topic");
    const std::string summary = kb::editor::AgentCarrySummary("Make the crate red\nand bigger", "Done. The crate is red.\nIt is also bigger.");
    Require(summary.find('\n') == std::string::npos && summary.find("crate red") != std::string::npos && summary.find("The crate is red") != std::string::npos &&
            summary.find("It is also bigger") == std::string::npos,
        "the summary is one line of the request and the first line of the answer");
}

void RunPlanTests() {
    using namespace kb::editor;
    const std::string answer =
        "Here is the plan:\n"
        "1. Add a floor and a crate || the crate stands on the floor || visual\n"
        "2) **Make the crate fall** || it falls when the scene plays || plain\n"
        "3. Count the falls in a script\n";
    const std::vector<EditorAgentPlanStep> steps = ParseAgentPlan(answer);
    Require(steps.size() == 3U, "three numbered lines are three steps");
    Require(steps[0].text == "Add a floor and a crate" && steps[0].check == "the crate stands on the floor" && steps[0].visual, "a step has its text, its check and whether it is visual");
    Require(steps[1].text == "Make the crate fall" && !steps[1].visual, "emphasis is dropped and a plain step is not visual");
    Require(steps[2].text == "Count the falls in a script" && steps[2].check.empty(), "a step with no check is kept");
    Require(ParseAgentPlan("1. Only one step || it works || plain").empty(), "a single step is no plan");
    Require(ParseAgentPlan("no list here").empty(), "an answer without a list is no plan");
    std::string long_answer;
    for (int step = 1; step <= 12; ++step) long_answer += std::to_string(step) + ". Step " + std::to_string(step) + " || ok || plain\n";
    const std::vector<EditorAgentPlanStep> capped = ParseAgentPlan(long_answer);
    Require(capped.size() == 10U && capped.back().text.find("Step 12") != std::string::npos, "a list of twelve keeps ten steps, the tail joined to the last");

    EditorAgentPlan plan;
    plan.goal = "Build a crate game";
    plan.steps = steps;
    plan.steps[0].progress = EditorAgentStepProgress::Done;
    const std::string request = AgentStepRequest(plan, 1U);
    Require(request.find("Build a crate game") != std::string::npos && request.find("- Add a floor and a crate") != std::string::npos &&
            request.find("This step: Make the crate fall") != std::string::npos && request.find("Done when: it falls when the scene plays") != std::string::npos &&
            request.find("Check passed:") != std::string::npos && request.find("capture") == std::string::npos,
        "a step's request holds the job, what is done, the step and its check");
    Require(AgentStepRequest(plan, 0U).find("capture") != std::string::npos, "a visual step is told to take a picture");
    Require(AgentPlanRequest("Build a crate game").find("Build a crate game") != std::string::npos, "the plan's request holds the job");

    Require(ReadAgentVerdict("I did it. Check passed: the crate fell.") == EditorAgentVerdict::Passed, "a passed check is read");
    Require(ReadAgentVerdict("check failed: nothing moved") == EditorAgentVerdict::Failed, "a failed check is read, in any case");
    Require(ReadAgentVerdict("Check failed: no floor\nthen I fixed it. Check passed: the floor is there") == EditorAgentVerdict::Passed, "the last verdict counts");
    Require(ReadAgentVerdict("All done.") == EditorAgentVerdict::Missing, "an answer with no verdict is not a pass");
}

void RunPriceTests() {
    using namespace kb::editor;
    kb::core::JsonValue document;
    std::string error;
    Require(kb::core::JsonValue::Parse(
        R"({"currency":"EUR","prices":{"model-a":{"input":2.0,"cacheRead":0.2,"cacheWrite":2.5,"output":10.0},"model-a-big":{"input":8.0,"output":40.0}}})", document, error),
        "the price table is JSON");
    const EditorAgentPrices prices = EditorAgentPrices::FromJson(document);
    Require(prices.Currency() == "EUR", "the table names its currency");
    const EditorAgentPrice* small = prices.Find("Vendor-Model-A-20260101");
    Require(small != nullptr && small->model == "model-a", "a model is found by a part of its name, in any case");
    const EditorAgentPrice* big = prices.Find("vendor-model-a-big-2");
    Require(big != nullptr && big->model == "model-a-big", "the longest matching name wins");
    Require(big->cacheRead == 8.0 && big->cacheWrite == 8.0, "a price left out is the input price");
    Require(prices.Find("another-model") == nullptr, "a model with no entry has no price");
    // 1M fresh input, 2M from cache, 0.5M written to it, 0.1M output.
    const double cost = EditorAgentPrices::Cost(*small, { 1'000'000U, 2'000'000U, 500'000U, 100'000U });
    Require(kb::editor::tests::NearlyEqual(cost, 2.0 + 0.4 + 1.25 + 1.0), "the cost adds each kind of token at its own price");
    kb::core::JsonValue none;
    Require(EditorAgentPrices::FromJson(none).Currency() == "USD" && EditorAgentPrices::FromJson(none).Find("model-a") == nullptr,
        "with no table there is no price and the currency is the default");
}

void RunListedModelsTests() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "21kb_editor_agent_guidance_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    const auto write = [&](const char* name, const char* text) {
        std::ofstream(root / name, std::ios::binary) << text;
        return root / name;
    };
    // A list with a slug and a shown name per model, some hidden, and the reasoning levels of one.
    const fs::path slugs = write("slugs.json",
        R"({"models":[{"slug":"m-a","display_name":"M-A","visibility":"list","supported_reasoning_levels":[{"effort":"low"},{"effort":"ultra"}]},{"slug":"m-b","visibility":"hide"},{"slug":"m-c"}]})");
    const std::vector<kb::editor::EditorAgentModel> listed = kb::editor::ListedEditorAgentModels(slugs);
    Require(listed.size() == 2U && listed[0].id == "m-a" && listed[1].id == "m-c" && listed[0].main && listed[1].main,
        "hidden models are left out, the file's order is kept");
    Require(listed[0].name == "M-A" && listed[1].name.empty(), "the shown name is the display name, empty when the file gives none");
    Require(listed[0].listsEfforts && listed[0].efforts.size() == 2U && listed[0].efforts[0].id == "low" && listed[0].efforts[1].id == "ultra",
        "the reasoning levels come from the model's own list");
    Require(!listed[1].listsEfforts && listed[1].efforts.empty(), "a model whose file says nothing about levels lists none");
    // A catalog with the same family twice, an overflow section, a repeated id, a hidden entry and the reasoning levels of two.
    const fs::path catalog = write("catalog.json",
        R"({"version":2,"catalog":{"config":{"models":[
            {"id":"x-1","name":"Model One","section":"main","thinking":{"type":"effort","effort_options":[{"id":"low","name":"Low"},{"id":"medium","name":"Medium","badge":{"message":"Recommended","variant":"neutral"}},{"id":"max","name":"Max","badge":{"message":"costly","variant":"warning"}}]},"runtime":{"family":"fam-a"}},
            {"id":"x-2","section":"main","thinking":{"type":"none"},"runtime":{"family":"fam-a"}},
            {"id":"x-3","name":"Model Three","section":"overflow","runtime":{"family":"fam-a"}},
            {"id":"x-0","name":"Model Zero","section":"overflow"},
            {"id":"x-1","section":"main"},
            {"id":"x-4","visibility":"hide"}]}}})");
    const std::vector<kb::editor::EditorAgentModel> catalogModels = kb::editor::ListedEditorAgentModels(catalog);
    Require(catalogModels.size() == 4U && catalogModels[0].id == "x-1" && catalogModels[1].id == "x-2" &&
        catalogModels[2].id == "x-3" && catalogModels[3].id == "x-0",
        "the main models come first, the file's order kept in each group");
    Require(catalogModels[0].main && catalogModels[1].main && !catalogModels[2].main && !catalogModels[3].main,
        "only the main section holds the main models");
    kb::editor::EditorAgent agent;
    agent.models = catalogModels;
    Require(agent.ModelName("x-1") == "Model One" && agent.ModelName("x-2") == "x-2" && agent.ModelName("alias") == "alias",
        "a model shows the name from the file, or its id when it has none");
    Require(agent.OffersModel("x-3") && !agent.OffersModel("alias"), "an agent offers exactly the models from its file");
    Require(catalogModels[0].family == "fam-a" && catalogModels[2].family == "fam-a", "the family comes from the runtime");
    Require(agent.ResolveModel("x-3") == "x-3", "a listed id stands for itself");
    Require(agent.ResolveModel("fam-a") == "x-1", "a family's short name is its first main model, not an extra one");
    Require(agent.ResolveModel("alias").empty() && agent.ResolveModel("").empty(), "anything else stands for no model");
    Require(catalogModels[0].listsEfforts && catalogModels[0].efforts.size() == 3U &&
        catalogModels[0].efforts[1].badge == "Recommended" && !catalogModels[0].efforts[1].warning && catalogModels[0].efforts[2].warning,
        "a model's levels carry the program's notes and warnings");
    Require(catalogModels[1].listsEfforts && catalogModels[1].efforts.empty(), "a model that does not reason lists no levels");
    Require(!catalogModels[2].listsEfforts, "a model whose file says nothing about levels leaves the agent's own list");
    agent.efforts = { "low", "high" };
    Require(agent.EffortsFor("x-1").size() == 3U, "a model's own levels win over the agent's");
    Require(agent.EffortsFor("x-2").empty(), "a model that does not reason takes no level");
    Require(agent.EffortsFor("x-3").size() == 2U && agent.EffortsFor("x-3")[0].id == "low" && agent.EffortsFor("x-3")[1].id == "high",
        "a model without levels of its own takes the agent's");
    Require(agent.EffortsFor("").size() == 2U && agent.EffortsFor("")[1].id == "high", "the default model takes the agent's list");
    Require(agent.EffortName("x-1", "medium") == "Medium" && agent.EffortName("x-3", "high") == "High",
        "a level shows the program's name for it, or its own word capitalized");
    Require(kb::editor::ListedEditorAgentModels(root / "missing.json").empty(), "no file, no models");
    Require(kb::editor::ListedEditorAgentModels(write("broken.json", "{ not json")).empty(), "a damaged file gives no models");
    fs::remove_all(root, error);
}

// Only the model the person chose works on a request: the command of every agent installed here forbids sub-agents, in the way that
// agent's program has (no other program has one the window could use).
void RunSubagentBanTests() {
    const auto holds = [](const std::vector<std::string>& words, const std::string& word) { return std::ranges::find(words, word) != words.end(); };
    for (const kb::editor::EditorAgent& agent : kb::editor::DetectEditorAgents()) {
        if (const auto banned = std::ranges::find(agent.runArguments, "--disallowedTools"); banned != agent.runArguments.end()) {
            Require(std::find(banned, agent.runArguments.end(), "Agent") != agent.runArguments.end() &&
                    std::find(banned, agent.runArguments.end(), "Task") != agent.runArguments.end(),
                "an agent started with tool rules has its sub-agent tools forbidden");
        } else if (agent.name == "Codex") {
            Require(holds(agent.runArguments, "multi_agent") && holds(agent.resumeRunArguments, "multi_agent"), "Codex is started with its sub-agents off, on a new thread and on a resumed one");
        } else if (agent.name == "Grok") {
            Require(holds(agent.runArguments, "--no-subagents"), "Grok is started without sub-agents");
        }
    }
}

} // namespace

namespace kb::editor::tests {

void RunEditorAgentGuidanceTests() {
    RunLoopGuardTests();
    RunSizeAndTierTests();
    RunPlanTests();
    RunPriceTests();
    RunListedModelsTests();
    RunSubagentBanTests();
}

} // namespace kb::editor::tests
