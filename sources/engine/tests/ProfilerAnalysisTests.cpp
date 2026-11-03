#include "engine/core/ProfilerDiagnosis.hpp"
#include "engine/core/ProfilerFrameRegions.hpp"
#include "engine/core/ProfilerRecorder.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using namespace kb::core;

void Require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}

constexpr std::uint64_t kMs = 1'000'000U;

// Recorded the way ProfilerCounters records them: a region when it ends, children first.
std::vector<ProfilerTimelineEvent> OneFrame() {
    return {
        { "Physics", 2 * kMs, 2 * kMs, 2U },
        { "Script", 5 * kMs, 1 * kMs, 2U },
        { "Script", 6 * kMs, 1 * kMs, 2U },
        { "Script", 7 * kMs, 1 * kMs, 2U },
        { "Update", 1 * kMs, 8 * kMs, 1U },
        { "Present", 10 * kMs, 5 * kMs, 1U },
        { "Frame", 0U, 16 * kMs, 0U },
    };
}

void RegionsNestAndSum() {
    const std::vector<ProfilerRegionRow> rows = ProfilerFrameRegions::Aggregate(OneFrame());
    Require(rows.size() == 5U, "Frame, Update, Physics, Script, Present");
    Require(rows[0].name == "Frame" && rows[1].name == "Update" && rows[2].name == "Physics" && rows[3].name == "Script" && rows[4].name == "Present", "Tree order: a region, then what is inside it");
    Require(rows[0].depth == 0U && rows[1].depth == 1U && rows[2].depth == 2U && rows[4].depth == 1U, "Depths follow the nesting");
    Require(rows[3].calls == 3U && rows[3].totalNanoseconds == 3 * kMs, "Three calls of one name under one parent are one row");
    Require(rows[1].selfNanoseconds == 3 * kMs, "Update (8 ms) less Physics (2) and the scripts (3) is 3 ms of its own");
    Require(rows[0].selfNanoseconds == 3 * kMs, "Frame (16 ms) less Update (8) and Present (5)");
    Require(rows[0].totalNanoseconds == 16 * kMs, "The outermost region is the frame");
    std::uint64_t selfTotal = 0U;
    for (const ProfilerRegionRow& row : rows) selfTotal += row.selfNanoseconds;
    Require(selfTotal == 16 * kMs, "The time of every region's own work adds up to the frame");
}

void SameNameUnderTwoParentsStaysTwoRows() {
    const std::vector<ProfilerRegionRow> rows = ProfilerFrameRegions::Aggregate({
        { "Tick", 1 * kMs, 1 * kMs, 2U }, { "A", 0U, 3 * kMs, 1U },
        { "Tick", 4 * kMs, 2 * kMs, 2U }, { "B", 3 * kMs, 3 * kMs, 1U },
        { "Frame", 0U, 6 * kMs, 0U } });
    Require(rows.size() == 5U && rows[2].name == "Tick" && rows[4].name == "Tick" && rows[2].totalNanoseconds == 1 * kMs && rows[4].totalNanoseconds == 2 * kMs,
        "A name is merged only under the same parent");
}

void HistoryKeepsTheLastFrames() {
    ProfilerFrameRegions frames;
    Require(frames.Latest() == nullptr && frames.Find(1U) == nullptr, "No frames at first");
    for (std::uint64_t frame = 1U; frame <= ProfilerFrameRegions::HistoryFrames() + 10U; ++frame) frames.Capture(frame, OneFrame());
    Require(frames.Size() == ProfilerFrameRegions::HistoryFrames(), "The history holds its capacity");
    Require(frames.OldestFrame() == 11U && frames.Find(10U) == nullptr && frames.Find(11U) != nullptr, "The oldest frames leave first");
    Require(frames.Latest()->frame == ProfilerFrameRegions::HistoryFrames() + 10U && frames.Latest()->FrameNanoseconds() == 16 * kMs, "The latest frame is the last captured");
}

void RecorderWritesTheRecordedRange() {
    ProfilerRegistry registry;
    const std::size_t frame = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
    const std::size_t draws = ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls);
    ProfilerRecorder recorder;
    registry.Publish(frame, 99.0);  // before the recording: not in it
    recorder.Start(registry, { frame, draws });
    Require(recorder.Recording() && recorder.Frames() == 0U, "A recording starts empty");
    for (std::uint64_t index = 10U; index < 14U; ++index) {
        registry.Publish(frame, 16.0 + static_cast<double>(index));
        if (index % 2U == 0U) registry.Publish(draws, 200.0 + static_cast<double>(index));
        recorder.Capture(registry, index, static_cast<double>(index - 10U) * 16.6);
    }
    recorder.Stop();
    recorder.Capture(registry, 14U, 80.0);
    Require(recorder.Frames() == 4U && recorder.FirstFrame() == 10U && recorder.LastFrame() == 13U, "The range is the frames captured while recording");
    std::istringstream csv{ recorder.ToCsv(registry) };
    std::vector<std::string> lines;
    for (std::string line; std::getline(csv, line);) lines.push_back(line);
    Require(lines.size() == 5U && lines[0] == "frame,time_ms,frame_ms,draw_calls", "A header, then a line per frame");
    Require(lines[1] == "10,0.000,26.0000,210.0000", "A fresh reading is written");
    Require(lines[2] == "11,16.600,27.0000,", "A counter without a fresh reading leaves its cell empty");
    Require(lines[4] == "13,49.800,29.0000,", "The last frame");
}

void RecorderStopsWhenFull() {
    ProfilerRegistry registry;
    ProfilerRecorder recorder;
    recorder.Start(registry, { ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime) });
    for (std::uint64_t index = 0U; index < ProfilerRecorder::MaxFrames() + 5U; ++index) recorder.Capture(registry, index, 0.0);
    Require(recorder.Frames() == ProfilerRecorder::MaxFrames() && recorder.Full() && !recorder.Recording(), "A full recording stops and says so");
    recorder.Start(registry, {});
    Require(!recorder.Full() && recorder.Frames() == 0U, "A new recording starts clean");
}

void RecorderQuotesFieldsThatNeedIt() {
    ProfilerRegistry registry;
    const auto counter = registry.RegisterCustom("System \"A,B\"", ProfilerArea::Ecs, ProfilerUnit::Milliseconds);
    ProfilerRecorder recorder;
    recorder.Start(registry, { *counter });
    Require(recorder.ToCsv(registry).starts_with("frame,time_ms,\"System \"\"A,B\"\"\"\n"), "A name with a comma and quotes is quoted");
}

// A registry whose frame, GPU and draw counters are published `frames` times.
void Fill(ProfilerRegistry& registry, ProfilerBuiltIn counter, double value, std::size_t frames = 240U) {
    for (std::size_t index = 0U; index < frames; ++index) registry.Publish(ProfilerRegistry::Id(counter), value);
}

[[nodiscard]] const DiagnosisFinding* Find(const std::vector<DiagnosisFinding>& findings, std::string_view rule) {
    const auto found = std::ranges::find(findings, rule, &DiagnosisFinding::rule);
    return found == findings.end() ? nullptr : &*found;
}

void HealthyFramesHaveNoFindings() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    Fill(registry, ProfilerBuiltIn::FrameTime, 10.0);
    Fill(registry, ProfilerBuiltIn::DrawCalls, 300.0);
    Fill(registry, ProfilerBuiltIn::ProcessMemory, 500.0);
    Require(ProfilerDiagnosis{}.Run({ registry, frames }).empty(), "Frames inside their budget raise nothing");
}

void FrameOverBudgetSaysWhoHoldsItUp() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    Fill(registry, ProfilerBuiltIn::FrameTime, 20.0);
    auto findings = ProfilerDiagnosis{}.Run({ registry, frames });
    const DiagnosisFinding* frame = Find(findings, "frame_over_budget");
    Require(frame && frame->severity == DiagnosisSeverity::Warning && frame->subject == "frame" && std::abs(frame->gainMilliseconds - 3.4) < 1e-9, "20 ms over a 16.6 ms budget is a warning worth 3.4 ms");
    Fill(registry, ProfilerBuiltIn::GpuTime, 19.0);
    frame = Find(findings = ProfilerDiagnosis{}.Run({ registry, frames }), "frame_over_budget");
    Require(frame && frame->subject == "gpu" && frame->category == DiagnosisCategory::Gpu, "A GPU that takes the frame is the one named");
    Fill(registry, ProfilerBuiltIn::FrameTime, 40.0);
    Require(Find(ProfilerDiagnosis{}.Run({ registry, frames }), "frame_over_budget")->severity == DiagnosisSeverity::Problem, "Past the alarm it is a problem");
}

void WorkCountersAgainstTheirThresholds() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    Fill(registry, ProfilerBuiltIn::DrawCalls, 3000.0);
    Fill(registry, ProfilerBuiltIn::RenderThreadTime, 6.0);
    Fill(registry, ProfilerBuiltIn::Triangles, 6'000'000.0);
    Fill(registry, ProfilerBuiltIn::GpuTime, 10.0);
    const auto findings = ProfilerDiagnosis{}.Run({ registry, frames });
    const DiagnosisFinding* draws = Find(findings, "draw_calls_high");
    const DiagnosisFinding* tris = Find(findings, "triangles_high");
    Require(draws && draws->severity == DiagnosisSeverity::Warning && std::abs(draws->gainMilliseconds - 2.0) < 1e-9, "3000 draws over 2000: a third of the 6 ms render thread");
    Require(tris && tris->severity == DiagnosisSeverity::Problem, "6 M triangles are past the 5 M alarm");
    registry.SetThresholds(ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls), 4000.0, 8000.0);
    Fill(registry, ProfilerBuiltIn::DrawCalls, 3000.0);
    Require(!Find(ProfilerDiagnosis{}.Run({ registry, frames }), "draw_calls_high"), "A threshold the user raised moves the finding");
}

void MemoryThatKeepsGrowing() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    for (std::size_t index = 0U; index < 240U; ++index) registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::ProcessMemory), 500.0);
    Require(!Find(ProfilerDiagnosis{}.Run({ registry, frames }), "memory_growth"), "Steady memory raises nothing");
    for (std::size_t index = 0U; index < 240U; ++index) registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::ProcessMemory), 500.0 + static_cast<double>(index));
    const DiagnosisFinding* growth = Find(ProfilerDiagnosis{}.Run({ registry, frames }), "memory_growth");
    Require(growth && growth->category == DiagnosisCategory::Memory && growth->value > 100.0, "Memory that rises 240 MB in 240 frames is growth");
}

void ARegionThatEatsTheFrame() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    Require(!Find(ProfilerDiagnosis{}.Run({ registry, frames }), "hot_region"), "No frame, no finding");
    frames.Capture(1U, OneFrame());  // Update holds 8 of 16 ms
    const auto findings = ProfilerDiagnosis{}.Run({ registry, frames });
    const DiagnosisFinding* hot = Find(findings, "hot_region");
    Require(hot && hot->subject == "Update" && std::abs(hot->value - 50.0) < 1e-9 && std::abs(hot->gainMilliseconds - 3.0) < 1e-9, "Update holds half the frame and 3 ms of its own work");
    Require(std::ranges::count(findings, std::string{ "hot_region" }, &DiagnosisFinding::rule) == 1, "Present (31%) and the rest are under the threshold");
}

// A frame of 20 ms whose Present takes 16, `waited` of them waiting for the previous frame.
std::vector<ProfilerTimelineEvent> FrameWithPresent(std::uint64_t waited) {
    return {
        { "Wait for the previous frame", 3 * kMs, waited, 2U, true },
        { "Present", 2 * kMs, 16 * kMs, 1U },
        { "Frame", 0U, 20 * kMs, 0U },
    };
}

void WaitingIsNotWork() {
    const std::vector<ProfilerRegionRow> rows = ProfilerFrameRegions::Aggregate(FrameWithPresent(14 * kMs));
    Require(rows.size() == 3U && rows[2].waiting && !rows[1].waiting && !rows[0].waiting, "Only the region recorded as waiting is waiting");
    Require(rows[2].waitNanoseconds == 14 * kMs && rows[1].waitNanoseconds == 14 * kMs && rows[0].waitNanoseconds == 14 * kMs, "The time waited counts for every region that contains the wait");
    Require(rows[1].selfNanoseconds == 2 * kMs, "Present less the wait is 2 ms of its own work");

    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    frames.Capture(1U, FrameWithPresent(14 * kMs));
    Require(!Find(ProfilerDiagnosis{}.Run({ registry, frames }), "hot_region"), "A Present that is 14 ms of waiting is not a region to reduce, and the wait itself is never named");
    frames.Capture(2U, FrameWithPresent(4 * kMs));
    const std::vector<DiagnosisFinding> findings = ProfilerDiagnosis{}.Run({ registry, frames });
    const DiagnosisFinding* hot = Find(findings, "hot_region");
    Require(hot && hot->subject == "Present" && std::abs(hot->value - 60.0) < 1e-9 && std::abs(hot->gainMilliseconds - 12.0) < 1e-9, "With 12 ms of real work in a 20 ms frame it is 60% and worth naming");
}

void SpikesAndTheOrderOfFindings() {
    ProfilerRegistry registry;
    ProfilerFrameRegions frames;
    for (std::size_t index = 0U; index < 240U; ++index) registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), index == 100U ? 50.0 : 10.0);
    auto findings = ProfilerDiagnosis{}.Run({ registry, frames });
    Require(findings.size() == 1U && findings[0].rule == "frame_spikes" && findings[0].severity == DiagnosisSeverity::Tip && findings[0].value == 1.0, "One frame over the alarm is a tip");
    for (std::size_t index = 0U; index < 240U; ++index) registry.Publish(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime), index % 40U == 0U ? 50.0 : 10.0);
    Fill(registry, ProfilerBuiltIn::DrawCalls, 3000.0);
    findings = ProfilerDiagnosis{}.Run({ registry, frames });
    Require(findings.size() == 2U && findings[0].rule == "draw_calls_high" && findings[1].rule == "frame_spikes" && findings[1].severity == DiagnosisSeverity::Warning,
        "Several spikes are a warning, and findings come out most severe first");
}

} // namespace

int main() {
    try {
        RegionsNestAndSum();
        SameNameUnderTwoParentsStaysTwoRows();
        HistoryKeepsTheLastFrames();
        RecorderWritesTheRecordedRange();
        RecorderStopsWhenFull();
        RecorderQuotesFieldsThatNeedIt();
        HealthyFramesHaveNoFindings();
        FrameOverBudgetSaysWhoHoldsItUp();
        WorkCountersAgainstTheirThresholds();
        MemoryThatKeepsGrowing();
        ARegionThatEatsTheFrame();
        WaitingIsNotWork();
        SpikesAndTheOrderOfFindings();
    } catch (const std::exception& error) {
        std::cerr << "Profiler analysis test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Profiler analysis tests passed\n";
    return 0;
}
