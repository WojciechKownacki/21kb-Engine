#include "engine/core/ProfilerDiagnosis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace kb::core {
namespace {

constexpr std::size_t kHistory = ProfilerRegistry::HistoryFrames();
// A region is worth a finding when it holds this part of its frame and at least this much time.
constexpr double kHotShare = 0.4;
constexpr double kHotMilliseconds = 1.0;
// Memory counts as growing at this much from the start to the end of the newer half of the history,
// when it is still rising in the end (a scene that has loaded and settled is not a leak).
constexpr double kGrowthMegabytes = 16.0;
constexpr double kGrowthFraction = 0.05;
constexpr double kNanosecondsPerMillisecond = 1'000'000.0;

[[nodiscard]] std::size_t Index(ProfilerBuiltIn counter) noexcept { return ProfilerRegistry::Id(counter); }

[[nodiscard]] DiagnosisSeverity SeverityOf(const ProfilerCounterInfo& info, double value) noexcept {
    return ProfilerRegistry::Severity(info, value) == ProfilerSeverity::Alarm ? DiagnosisSeverity::Problem : DiagnosisSeverity::Warning;
}

// The frame, over its budget; the finding says who holds it up.
class FrameOverBudgetRule final : public DiagnosisRule {
public:
    std::string_view Id() const noexcept override { return "frame_over_budget"; }
    void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const override {
        const ProfilerCounterInfo& info = input.registry.Info(Index(ProfilerBuiltIn::FrameTime));
        const ProfilerCounterStats frame = input.registry.Stats(Index(ProfilerBuiltIn::FrameTime));
        if (!frame.available || info.budget <= 0.0 || frame.p95 <= info.budget) return;
        const ProfilerCounterStats gpu = input.registry.Stats(Index(ProfilerBuiltIn::GpuTime));
        const ProfilerCounterStats game = input.registry.Stats(Index(ProfilerBuiltIn::GameThreadTime));
        // The side that takes most of the frame is the one to look at first.
        const bool gpuBound = gpu.available && gpu.p95 >= 0.8 * frame.p95;
        const bool gameBound = !gpuBound && game.available && game.average >= 0.5 * frame.average;
        findings.push_back(DiagnosisFinding{ .rule = std::string{ Id() }, .severity = SeverityOf(info, frame.p95),
            .category = gpuBound ? DiagnosisCategory::Gpu : (gameBound ? DiagnosisCategory::Cpu : DiagnosisCategory::Frame),
            .subject = gpuBound ? "gpu" : (gameBound ? "game" : "frame"), .value = frame.p95, .limit = info.budget,
            .gainMilliseconds = frame.p95 - info.budget });
    }
};

// A counter that counts work (draw calls, triangles) over its threshold; the gain is the share of
// the thread that does that work which the excess takes.
class WorkOverThresholdRule final : public DiagnosisRule {
public:
    WorkOverThresholdRule(std::string id, ProfilerBuiltIn counter, ProfilerBuiltIn timeCounter, DiagnosisCategory category)
        : id_(std::move(id)), counter_(counter), timeCounter_(timeCounter), category_(category) {}
    std::string_view Id() const noexcept override { return id_; }
    void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const override {
        const ProfilerCounterInfo& info = input.registry.Info(Index(counter_));
        const ProfilerCounterStats work = input.registry.Stats(Index(counter_));
        if (!work.available || info.budget <= 0.0 || work.p95 <= info.budget) return;
        const ProfilerCounterStats time = input.registry.Stats(Index(timeCounter_));
        const double gain = time.available ? time.average * (work.p95 - info.budget) / work.p95 : 0.0;
        findings.push_back(DiagnosisFinding{ .rule = id_, .severity = SeverityOf(info, work.p95), .category = category_,
            .subject = std::string{ info.id }, .value = work.p95, .limit = info.budget, .gainMilliseconds = gain });
    }

private:
    std::string id_;
    ProfilerBuiltIn counter_;
    ProfilerBuiltIn timeCounter_;
    DiagnosisCategory category_;
};

// Process memory that keeps rising through the newer half of the history: in its four parts the
// last is well above the first, and the last step is still a good part of the whole rise.
class MemoryGrowthRule final : public DiagnosisRule {
public:
    std::string_view Id() const noexcept override { return "memory_growth"; }
    void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const override {
        std::array<double, kHistory> samples{};
        const std::size_t count = input.registry.Samples(Index(ProfilerBuiltIn::ProcessMemory), samples);
        if (count < kHistory / 2U) return;
        constexpr std::size_t kParts = 4U;
        const std::size_t part = (count - count / 2U) / kParts;
        const std::size_t begin = count - kParts * part;
        const auto mean = [&](std::size_t index) {
            const auto from = samples.begin() + static_cast<std::ptrdiff_t>(begin + index * part);
            return std::accumulate(from, from + static_cast<std::ptrdiff_t>(part), 0.0) / static_cast<double>(part);
        };
        const double first = mean(0U);
        const double growth = mean(kParts - 1U) - first;
        const double lastStep = mean(kParts - 1U) - mean(kParts - 2U);
        if (growth < std::max(kGrowthMegabytes, first * kGrowthFraction) || lastStep < growth / 6.0) return;
        findings.push_back(DiagnosisFinding{ .rule = std::string{ Id() }, .severity = DiagnosisSeverity::Warning, .category = DiagnosisCategory::Memory,
            .subject = "process_memory_mb", .value = growth, .limit = static_cast<double>(count) });
    }
};

// A region whose own work holds a large part of its frame. The own time is what the region spends
// outside the regions inside it, so a container (a tick, a present) that only holds other regions
// is not named, and neither is waiting (for the previous frame, for a vsync): it is no work that
// can be made smaller.
class HotRegionRule final : public DiagnosisRule {
public:
    std::string_view Id() const noexcept override { return "hot_region"; }
    void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const override {
        const ProfilerFrameRecord* frame = input.frames.Latest();
        if (frame == nullptr || frame->FrameNanoseconds() == 0U) return;
        const double frameNanoseconds = static_cast<double>(frame->FrameNanoseconds());
        std::vector<const ProfilerRegionRow*> hot;
        const auto work = [](const ProfilerRegionRow& row) { return static_cast<double>(row.selfNanoseconds); };
        for (const ProfilerRegionRow& row : frame->rows) {
            if (row.waiting || row.depth < 1U) continue;
            if (work(row) / frameNanoseconds >= kHotShare && work(row) >= kHotMilliseconds * kNanosecondsPerMillisecond) hot.push_back(&row);
        }
        std::ranges::sort(hot, [&](const ProfilerRegionRow* a, const ProfilerRegionRow* b) { return work(*a) > work(*b); });
        constexpr std::size_t kMost = 3U;
        for (std::size_t index = 0U; index < std::min(hot.size(), kMost); ++index) {
            findings.push_back(DiagnosisFinding{ .rule = std::string{ Id() }, .severity = DiagnosisSeverity::Warning, .category = DiagnosisCategory::Cpu,
                .subject = hot[index]->name, .value = 100.0 * work(*hot[index]) / frameNanoseconds,
                .limit = 100.0 * kHotShare, .gainMilliseconds = work(*hot[index]) / kNanosecondsPerMillisecond });
        }
    }
};

// Frames that cost far more than their neighbours.
class FrameSpikesRule final : public DiagnosisRule {
public:
    std::string_view Id() const noexcept override { return "frame_spikes"; }
    void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const override {
        const ProfilerCounterInfo& info = input.registry.Info(Index(ProfilerBuiltIn::FrameTime));
        std::array<double, kHistory> samples{};
        const std::size_t count = input.registry.Samples(Index(ProfilerBuiltIn::FrameTime), samples);
        if (count < kHistory / 4U || info.alarm <= 0.0) return;
        const double over = static_cast<double>(std::count_if(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count), [&](double v) { return v > info.alarm; }));
        if (over < 1.0) return;
        findings.push_back(DiagnosisFinding{ .rule = std::string{ Id() }, .severity = over >= 3.0 ? DiagnosisSeverity::Warning : DiagnosisSeverity::Tip,
            .category = DiagnosisCategory::Frame, .subject = "frame_ms", .value = over, .limit = info.alarm });
    }
};

} // namespace

ProfilerDiagnosis::ProfilerDiagnosis() {
    rules_.push_back(std::make_unique<FrameOverBudgetRule>());
    rules_.push_back(std::make_unique<WorkOverThresholdRule>("draw_calls_high", ProfilerBuiltIn::DrawCalls, ProfilerBuiltIn::RenderThreadTime, DiagnosisCategory::Cpu));
    rules_.push_back(std::make_unique<WorkOverThresholdRule>("triangles_high", ProfilerBuiltIn::Triangles, ProfilerBuiltIn::GpuTime, DiagnosisCategory::Gpu));
    rules_.push_back(std::make_unique<MemoryGrowthRule>());
    rules_.push_back(std::make_unique<HotRegionRule>());
    rules_.push_back(std::make_unique<FrameSpikesRule>());
}

ProfilerDiagnosis::~ProfilerDiagnosis() = default;

std::vector<DiagnosisFinding> ProfilerDiagnosis::Run(const DiagnosisInput& input) const {
    std::vector<DiagnosisFinding> findings;
    for (const auto& rule : rules_) rule->Evaluate(input, findings);
    std::ranges::stable_sort(findings, [](const DiagnosisFinding& a, const DiagnosisFinding& b) {
        return a.severity != b.severity ? a.severity < b.severity : a.gainMilliseconds > b.gainMilliseconds;
    });
    return findings;
}

} // namespace kb::core
