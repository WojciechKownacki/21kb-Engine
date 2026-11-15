#include "app/EditorStartupProgress.hpp"

#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace kb::editor {
namespace {

// Relative cost of each stage; they add up to 100.
constexpr std::array<std::uint32_t, kEditorStartupStageCount> kStageShare{ 5U, 15U, 20U, 15U, 10U, 35U };
constexpr double kShareTotal = 100.0;
// Only Finish reaches 1: a last stage that has all its items must not read as done early.
constexpr double kBeforeFinishLimit = 0.99;

[[nodiscard]] double ShareBefore(std::size_t stage) noexcept {
    std::uint32_t sum = 0U;
    for (std::size_t index = 0U; index < stage && index < kStageShare.size(); ++index) {
        sum += kStageShare[index];
    }
    return static_cast<double>(sum);
}

struct ProgressState {
    EditorStartupProgressModel model;
    EditorStartupProgress::Sink sink;
    std::string stage;
    std::string detail;
};

[[nodiscard]] ProgressState& State() {
    static ProgressState state;
    return state;
}

void Publish(ProgressState& state) {
    if (!state.sink) {
        return;
    }
    state.sink(EditorStartupSnapshot{
        .fraction = state.model.Fraction(),
        .stage = state.stage.empty() ? EditorStartupProgress::StageText(EditorStartupStage::Project) : state.stage,
        .detail = state.detail,
    });
}

} // namespace

void EditorStartupProgressModel::Raise(double value) noexcept {
    fraction_ = std::max(fraction_, std::min(value, kBeforeFinishLimit));
}

void EditorStartupProgressModel::Begin(EditorStartupStage stage) noexcept {
    const std::size_t index = static_cast<std::size_t>(stage);
    if (finished_ || (started_ && index <= stage_)) {
        return;
    }
    stage_ = index;
    started_ = true;
    inner_ = 0.0;
    Raise(ShareBefore(stage_) / kShareTotal);
}

void EditorStartupProgressModel::SetItems(std::size_t done, std::size_t total) noexcept {
    if (!started_ || finished_ || total == 0U) {
        return;
    }
    const double ratio = static_cast<double>(std::min(done, total)) / static_cast<double>(total);
    inner_ = std::max(inner_, ratio);
    Raise((ShareBefore(stage_) + static_cast<double>(kStageShare[stage_]) * inner_) / kShareTotal);
}

void EditorStartupProgressModel::Finish() noexcept {
    finished_ = true;
    fraction_ = 1.0;
}

double EditorStartupProgressModel::Fraction() const noexcept {
    return finished_ ? 1.0 : fraction_;
}

void EditorStartupProgress::SetSink(Sink sink) {
    ProgressState& state = State();
    state.sink = std::move(sink);
    Publish(state);
}

void EditorStartupProgress::Begin(EditorStartupStage stage) {
    ProgressState& state = State();
    state.model.Begin(stage);
    if (state.sink) {
        state.stage = StageText(stage);
    }
    state.detail.clear();
    Publish(state);
}

void EditorStartupProgress::Note(std::string_view detail) {
    ProgressState& state = State();
    if (!state.sink) {
        return;
    }
    state.detail = std::string{ detail };
    Publish(state);
}

void EditorStartupProgress::Shaders(std::size_t done, std::size_t total, std::string_view name) {
    ProgressState& state = State();
    state.model.SetItems(done, total);
    if (!state.sink) {
        return;
    }
    state.detail = ShadersText(done, total, name);
    Publish(state);
}

void EditorStartupProgress::Finish() {
    ProgressState& state = State();
    state.model.Finish();
    if (state.sink) {
        state.stage = FinishedText();
    }
    state.detail.clear();
    Publish(state);
}

double EditorStartupProgress::Fraction() {
    return State().model.Fraction();
}

std::string EditorStartupProgress::StageText(EditorStartupStage stage) {
    switch (stage) {
    case EditorStartupStage::Project:
        return std::string{ EditorText("startup.stage.project", "Opening project") };
    case EditorStartupStage::Modules:
        return std::string{ EditorText("startup.stage.modules", "Loading modules and plugins") };
    case EditorStartupStage::Assets:
        return std::string{ EditorText("startup.stage.assets", "Scanning project assets") };
    case EditorStartupStage::Scene:
        return std::string{ EditorText("startup.stage.scene", "Loading scene") };
    case EditorStartupStage::Interface:
        return std::string{ EditorText("startup.stage.interface", "Preparing the editor window") };
    case EditorStartupStage::Renderer:
        return std::string{ EditorText("startup.stage.renderer", "Starting the renderer and loading shaders") };
    }
    return {};
}

std::string EditorStartupProgress::FinishedText() {
    return std::string{ EditorText("startup.stage.ready", "Opening the editor") };
}

std::string EditorStartupProgress::PluginsText(std::size_t count) {
    return EditorLocalization::Format("startup.plugins", "Plugins: {count}", { { "count", std::to_string(count) } });
}

std::string EditorStartupProgress::ShadersText(std::size_t done, std::size_t total, std::string_view name) {
    std::string text = EditorLocalization::Format("startup.shaders", "Shaders {done} / {total}",
        { { "done", std::to_string(done) }, { "total", std::to_string(total) } });
    if (!name.empty()) {
        text += "  \xC2\xB7  ";
        text += name;
    }
    return text;
}

} // namespace kb::editor
