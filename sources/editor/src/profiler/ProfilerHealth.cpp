#include "profiler/ProfilerHealth.hpp"

#include "localization/EditorLocalization.hpp"
#include "profiler/ProfilerNumber.hpp"
#include "profiler/ProfilerOverlayModel.hpp"
#include "profiler/ProfilerText.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerCounterInfo;
using kb::core::ProfilerCounterStats;
using kb::core::ProfilerRegistry;
using kb::core::ProfilerSeverity;

[[nodiscard]] ProfilerCounterStats StatsOf(const ProfilerRegistry& registry, ProfilerBuiltIn counter) {
    return registry.Stats(ProfilerRegistry::Id(counter));
}

// The part of the frame that takes the longest, said the way someone who never read a
// profiler would say it. Empty when the parts were not measured.
[[nodiscard]] std::string Culprit(const ProfilerRegistry& registry, const ProfilerSelection& selection) {
    const ProfilerCounterStats game = StatsOf(registry, ProfilerBuiltIn::GameThreadTime);
    const ProfilerCounterStats render = StatsOf(registry, ProfilerBuiltIn::RenderThreadTime);
    const ProfilerCounterStats gpu = StatsOf(registry, ProfilerBuiltIn::GpuTime);
    const double gameTime = game.available ? game.average : 0.0;
    const double renderTime = render.available ? render.average : 0.0;
    const double gpuTime = gpu.available && selection.AreaMeasured(ProfilerArea::Gpu) ? gpu.average : 0.0;
    const double longest = std::max({ gameTime, renderTime, gpuTime });
    if (longest <= 0.0) {
        return {};
    }
    const std::string time = ProfilerNumber::Fixed(longest, 1);
    if (longest == gpuTime) {
        return EditorLocalization::Format("profiler.health.culprit_gpu", "The graphics card takes the longest: {ms} ms.", { { "ms", time } });
    }
    if (longest == renderTime) {
        return EditorLocalization::Format("profiler.health.culprit_render", "Preparing the picture takes the longest: {ms} ms.", { { "ms", time } });
    }
    return EditorLocalization::Format("profiler.health.culprit_game", "Game code takes the longest: {ms} ms.", { { "ms", time } });
}

[[nodiscard]] ProfilerHealthCard CardOf(const ProfilerRegistry& registry, ProfilerBuiltIn counter, std::string detail) {
    const std::size_t id = ProfilerRegistry::Id(counter);
    const ProfilerCounterInfo& info = registry.Info(id);
    const ProfilerCounterStats stats = registry.Stats(id);
    ProfilerHealthCard card;
    card.label = std::string{ ProfilerText::Counter(info) };
    card.available = stats.available;
    card.value = stats.available ? FormatProfilerValue(info, stats.now) : std::string{ "-" };
    card.detail = std::move(detail);
    card.severity = stats.available ? ProfilerRegistry::Severity(info, stats.now) : ProfilerSeverity::Normal;
    return card;
}

// "render 2.1 ms": a second reading under a card's own.
[[nodiscard]] std::string Companion(const ProfilerRegistry& registry, ProfilerBuiltIn counter) {
    const std::size_t id = ProfilerRegistry::Id(counter);
    const ProfilerCounterStats stats = registry.Stats(id);
    if (!stats.available) {
        return {};
    }
    return std::string{ ProfilerText::Counter(registry.Info(id)) } + " " + FormatProfilerValue(registry.Info(id), stats.now);
}

} // namespace

ProfilerHealth BuildProfilerHealth(const ProfilerRegistry& registry, const ProfilerSelection& selection) {
    ProfilerHealth health;
    const ProfilerCounterInfo& frameInfo = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
    const ProfilerCounterStats frame = StatsOf(registry, ProfilerBuiltIn::FrameTime);
    const double budget = frameInfo.budget;
    const std::string budgetText = ProfilerNumber::General(budget);
    const std::string targetFps = budget > 0.0 ? ProfilerNumber::Fixed(1000.0 / budget, 0) : std::string{};

    if (!selection.AreaMeasured(ProfilerArea::Cpu)) {
        health.headline = std::string{ EditorText("profiler.health.off", "Frame time is not measured") };
        health.advice = std::string{ EditorText("profiler.health.off_advice", "Check CPU under Areas to see how fast the game runs.") };
    } else if (!frame.available || frame.samples == 0U || frame.average <= 0.0) {
        health.headline = std::string{ EditorText("profiler.health.waiting", "Waiting for frames") };
        health.advice = std::string{ EditorText("profiler.health.waiting_advice", "The numbers appear with the first measured frame.") };
    } else {
        const std::string fps = ProfilerNumber::Fixed(1000.0 / frame.average, 0);
        const std::string average = ProfilerNumber::Fixed(frame.average, 1);
        const double alarm = frameInfo.alarm > 0.0 ? frameInfo.alarm : budget * 2.0;
        if (budget > 0.0 && frame.average > budget) {
            health.level = frame.average > alarm ? ProfilerHealthLevel::TooSlow : ProfilerHealthLevel::BelowTarget;
            health.headline = health.level == ProfilerHealthLevel::TooSlow
                ? EditorLocalization::Format("profiler.health.slow", "Too slow: {fps} FPS", { { "fps", fps } })
                : EditorLocalization::Format("profiler.health.below", "Below the target: {fps} FPS", { { "fps", fps } });
            health.advice = EditorLocalization::Format("profiler.health.slow_advice",
                "A frame takes {ms} ms; the target is {budget} ms ({target} FPS).",
                { { "ms", average }, { "budget", budgetText }, { "target", targetFps } });
            if (const std::string culprit = Culprit(registry, selection); !culprit.empty()) {
                health.advice += " " + culprit;
            }
        } else if (budget > 0.0 && frame.p95 > budget) {
            health.level = ProfilerHealthLevel::Hitches;
            health.headline = EditorLocalization::Format("profiler.health.hitches", "Smooth, with hitches: {fps} FPS", { { "fps", fps } });
            health.advice = EditorLocalization::Format("profiler.health.hitches_advice",
                "Most frames fit the {budget} ms target, but the slow ones take {p95} ms.",
                { { "budget", budgetText }, { "p95", ProfilerNumber::Fixed(frame.p95, 1) } });
        } else {
            health.level = ProfilerHealthLevel::Smooth;
            health.headline = EditorLocalization::Format("profiler.health.smooth", "Runs smoothly: {fps} FPS", { { "fps", fps } });
            health.advice = budget > 0.0
                ? EditorLocalization::Format("profiler.health.smooth_advice", "Every frame fits the {budget} ms target ({target} FPS).",
                      { { "budget", budgetText }, { "target", targetFps } })
                : EditorLocalization::Format("profiler.health.no_target_advice", "A frame takes {ms} ms.", { { "ms", average } });
        }
    }

    // The numbers, in the order people ask for them. An area that is not measured has no card.
    if (selection.AreaMeasured(ProfilerArea::Cpu)) {
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::FramesPerSecond,
            targetFps.empty() ? std::string{} : EditorLocalization::Format("profiler.health.card_target", "target {fps}", { { "fps", targetFps } })));
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::FrameTime,
            budget > 0.0 ? EditorLocalization::Format("profiler.health.card_budget", "target {ms} ms", { { "ms", budgetText } }) : std::string{}));
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::GameThreadTime, Companion(registry, ProfilerBuiltIn::RenderThreadTime)));
    }
    if (selection.AreaMeasured(ProfilerArea::Gpu)) {
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::GpuTime, {}));
    }
    if (selection.AreaMeasured(ProfilerArea::Render)) {
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::DrawCalls, Companion(registry, ProfilerBuiltIn::Triangles)));
    }
    if (selection.AreaMeasured(ProfilerArea::Memory)) {
        health.cards.push_back(CardOf(registry, ProfilerBuiltIn::ProcessMemory, Companion(registry, ProfilerBuiltIn::GpuMemory)));
    }
    return health;
}

} // namespace kb::editor
