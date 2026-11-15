#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorStartupProgress.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace {

using kb::editor::EditorStartupProgress;
using kb::editor::EditorStartupProgressModel;
using kb::editor::EditorStartupSnapshot;
using kb::editor::EditorStartupStage;
using kb::editor::kEditorStartupStageCount;
using kb::editor::tests::NearlyEqual;
using kb::editor::tests::Require;

[[nodiscard]] EditorStartupStage Stage(std::size_t index) {
    return static_cast<EditorStartupStage>(index);
}

void RunModelOrderTest() {
    EditorStartupProgressModel model;
    Require(model.Fraction() == 0.0 && !model.Finished(), "A new progress model should start at 0");
    double last = 0.0;
    for (std::size_t index = 0U; index < kEditorStartupStageCount; ++index) {
        model.Begin(Stage(index));
        Require(model.Fraction() >= last, "Beginning the next stage should not lower the bar");
        last = model.Fraction();
        for (std::size_t item = 0U; item <= 50U; ++item) {
            model.SetItems(item, 50U);
            Require(model.Fraction() >= last, "More items done should not lower the bar");
            Require(model.Fraction() < 1.0, "The bar should stay below 100 % until the end is reported");
            last = model.Fraction();
        }
    }
    model.Finish();
    Require(model.Finished() && model.Fraction() == 1.0, "Finish should put the bar at exactly 100 %");
}

void RunUnknownLengthStageTest() {
    EditorStartupProgressModel model;
    model.Begin(EditorStartupStage::Project);
    model.Begin(EditorStartupStage::Modules);
    const double atStart = model.Fraction();
    Require(atStart > 0.0, "A finished stage should have added its share");
    model.SetItems(10U, 0U);
    Require(NearlyEqual(model.Fraction(), atStart), "Items of unknown length should not move the bar");
    model.Begin(EditorStartupStage::Assets);
    Require(model.Fraction() > atStart, "The next stage should add the share of a stage that reported no items");
}

void RunNeverBackwardsTest() {
    EditorStartupProgressModel model;
    model.Begin(EditorStartupStage::Scene);
    model.SetItems(40U, 100U);
    const double reached = model.Fraction();
    model.SetItems(10U, 100U);
    Require(model.Fraction() == reached, "Fewer items done than before should not lower the bar");
    model.Begin(EditorStartupStage::Project);
    Require(model.Fraction() == reached, "An earlier stage should not lower the bar");
    model.Begin(EditorStartupStage::Scene);
    Require(model.Fraction() == reached, "The same stage again should not lower the bar");
    model.SetItems(500U, 100U);
    Require(model.Fraction() < 1.0 && model.Fraction() >= reached, "More items than the total should count as all of them");
}

void RunReporterTest() {
    std::vector<EditorStartupSnapshot> seen;
    EditorStartupProgress::SetSink([&seen](const EditorStartupSnapshot& snapshot) { seen.push_back(snapshot); });
    Require(seen.size() == 1U && !seen.front().stage.empty(), "A new sink should be handed the current state at once");
    EditorStartupProgress::Begin(EditorStartupStage::Renderer);
    EditorStartupProgress::Shaders(1U, 10U, "fs_first.sc");
    EditorStartupProgress::Shaders(2U, 10U, "fs_second.sc");
    EditorStartupProgress::Note("a note");
    Require(seen.back().detail == "a note", "A note should replace the detail line");
    EditorStartupProgress::Finish();
    Require(seen.back().fraction == 1.0 && seen.back().detail.empty() && seen.back().stage == EditorStartupProgress::FinishedText(),
        "Finish should report 100 % and the closing text");
    for (std::size_t index = 1U; index < seen.size(); ++index) {
        Require(seen[index].fraction >= seen[index - 1U].fraction, "The reported progress should never decrease");
    }
    const std::string shaders = EditorStartupProgress::ShadersText(2U, 10U, "fs_second.sc");
    Require(shaders.find("2 / 10") != std::string::npos && shaders.find("fs_second.sc") != std::string::npos,
        "The shader line should name the count and the shader");
    EditorStartupProgress::SetSink({});
    const std::size_t count = seen.size();
    EditorStartupProgress::Note("after");
    Require(seen.size() == count, "A removed sink should hear nothing more");
}

} // namespace

namespace kb::editor::tests {

void RunEditorStartupProgressTests() {
    RunModelOrderTest();
    RunUnknownLengthStageTest();
    RunNeverBackwardsTest();
    RunReporterTest();
}

} // namespace kb::editor::tests
