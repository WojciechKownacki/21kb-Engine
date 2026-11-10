#include "app/EditorStartupSplashChecks.hpp"

#if defined(_WIN32)
#include "app/EditorHeadlessAutomation.hpp"
#include "app/EditorStartupProgress.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/EditorSplashPainter.hpp"
#include "rendering/gdi/CutTextAudit.hpp"
#include "rendering/gdi/EditorFontCache.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

constexpr std::array<int, 2> kDpis{ 96, 192 };
// A scene name much longer than most, still within what the line has room for.
constexpr std::string_view kLongSceneName = "Chapter 03 - The Sunken Archive of the Northern Reach - Layout 02";

struct Part {
    const char* name;
    RECT rect;
    int fontPixels;
};

[[nodiscard]] std::array<Part, 6> Parts(const EditorSplashLayout& layout) {
    return { {
        { "mark", layout.mark, layout.markFont },
        { "title", layout.title, layout.titleFont },
        { "stage", layout.stage, layout.stageFont },
        { "percent", layout.percent, layout.detailFont },
        { "track", layout.track, 0 },
        { "detail", layout.detail, layout.detailFont },
    } };
}

[[nodiscard]] bool Inside(const RECT& inner, const RECT& outer, int margin) noexcept {
    return inner.left >= outer.left + margin && inner.top >= outer.top + margin && inner.right <= outer.right - margin &&
        inner.bottom <= outer.bottom - margin && inner.right > inner.left && inner.bottom > inner.top;
}

[[nodiscard]] bool Doubled(const RECT& base, const RECT& scaled) noexcept {
    return scaled.left == base.left * 2 && scaled.top == base.top * 2 && scaled.right == base.right * 2 &&
        scaled.bottom == base.bottom * 2;
}

// One scratch bitmap the screen is painted into, so text can be checked against what GDI draws.
class ScratchDc {
public:
    explicit ScratchDc(const RECT& client) {
        screen_ = GetDC(nullptr);
        dc_ = screen_ == nullptr ? nullptr : CreateCompatibleDC(screen_);
        bitmap_ = dc_ == nullptr ? nullptr : CreateCompatibleBitmap(screen_, client.right, client.bottom);
        previous_ = bitmap_ == nullptr ? nullptr : SelectObject(dc_, bitmap_);
    }
    ~ScratchDc() {
        if (previous_ != nullptr) SelectObject(dc_, previous_);
        if (bitmap_ != nullptr) DeleteObject(bitmap_);
        if (dc_ != nullptr) DeleteDC(dc_);
        if (screen_ != nullptr) ReleaseDC(nullptr, screen_);
    }
    ScratchDc(const ScratchDc&) = delete;
    ScratchDc& operator=(const ScratchDc&) = delete;

    [[nodiscard]] HDC Get() const noexcept {
        return previous_ == nullptr ? nullptr : dc_;
    }

private:
    HDC screen_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

[[nodiscard]] EditorStartupSnapshot State(double fraction, EditorStartupStage stage, std::string detail) {
    return EditorStartupSnapshot{ fraction, EditorStartupProgress::StageText(stage), std::move(detail) };
}

[[nodiscard]] std::string LongestShaderName() {
    std::string longest;
    for (const std::string_view name : kb::render::RequiredPackagedShaderNames(~kb::render::ShaderRuntimeFeatureMask{ 0U })) {
        if (name.size() > longest.size()) {
            longest = std::string{ name };
        }
    }
    return longest;
}

// The screen in the worst state of every stage, as the current language words it.
[[nodiscard]] std::vector<EditorStartupSnapshot> WorstStates(const std::string& longestShader) {
    std::vector<EditorStartupSnapshot> states;
    states.push_back(State(0.0, EditorStartupStage::Project, {}));
    states.push_back(State(0.2, EditorStartupStage::Modules, EditorStartupProgress::PluginsText(120U)));
    states.push_back(State(0.4, EditorStartupStage::Assets, {}));
    states.push_back(State(0.5, EditorStartupStage::Scene, std::string{ kLongSceneName }));
    states.push_back(State(0.6, EditorStartupStage::Interface, {}));
    states.push_back(State(0.99, EditorStartupStage::Renderer, EditorStartupProgress::ShadersText(120U, 120U, longestShader)));
    states.push_back(EditorStartupSnapshot{ 1.0, EditorStartupProgress::FinishedText(), {} });
    return states;
}

} // namespace

std::pair<bool, std::string> EditorStartupSplashChecks::Run(EditorHeadlessAutomation& automation) {
    std::string problems;
    const auto fail = [&problems](const std::string& text) { problems += (problems.empty() ? "" : "; ") + text; };

    // Layout: the same picture at 200 % as at 100 %, every part inside the window and apart from the others,
    // and every line of text tall enough for its font.
    const EditorSplashLayout base = EditorSplashPainter::Layout(96);
    const EditorSplashLayout doubled = EditorSplashPainter::Layout(192);
    if (!Doubled(base.client, doubled.client) || doubled.titleFont != base.titleFont * 2 ||
        doubled.stageFont != base.stageFont * 2 || doubled.detailFont != base.detailFont * 2 ||
        doubled.markFont != base.markFont * 2) {
        fail("the 200 % DPI window and fonts are not twice the 100 % ones");
    }
    const std::array<Part, 6> baseParts = Parts(base);
    const std::array<Part, 6> doubledParts = Parts(doubled);
    for (std::size_t index = 0U; index < baseParts.size(); ++index) {
        if (!Doubled(baseParts[index].rect, doubledParts[index].rect)) {
            fail(std::string{ "the " } + baseParts[index].name + " is not twice as large at 200 % DPI");
        }
    }
    {
        const ScratchDc scratch(doubled.client);
        for (const int dpi : kDpis) {
            const EditorSplashLayout layout = EditorSplashPainter::Layout(dpi);
            const std::array<Part, 6> parts = Parts(layout);
            for (std::size_t index = 0U; index < parts.size(); ++index) {
                const Part& part = parts[index];
                if (!Inside(part.rect, layout.client, layout.border)) {
                    fail(std::string{ "the " } + part.name + " is outside the window at " + std::to_string(dpi) + " DPI");
                }
                for (std::size_t other = index + 1U; other < parts.size(); ++other) {
                    RECT overlap{};
                    if (IntersectRect(&overlap, &part.rect, &parts[other].rect) != 0) {
                        fail(std::string{ "the " } + part.name + " overlaps the " + parts[other].name + " at " +
                            std::to_string(dpi) + " DPI");
                    }
                }
                if (part.fontPixels > 0 && scratch.Get() != nullptr) {
                    const HGDIOBJ previous = SelectObject(scratch.Get(), EditorFontCache::Acquire(part.fontPixels, FW_NORMAL));
                    TEXTMETRICW metrics{};
                    GetTextMetricsW(scratch.Get(), &metrics);
                    SelectObject(scratch.Get(), previous);
                    if (metrics.tmHeight > part.rect.bottom - part.rect.top) {
                        fail(std::string{ "the " } + part.name + " is shorter than its text at " + std::to_string(dpi) + " DPI");
                    }
                }
            }
        }
    }

    // The bar: no fill at 0, the whole track at 1, and never narrower as the progress grows - checked on the
    // fractions the real stages produce as well as on a plain sweep.
    std::vector<double> fractions;
    for (int step = 0; step <= 1000; ++step) {
        fractions.push_back(static_cast<double>(step) / 1000.0);
    }
    EditorStartupProgressModel model;
    std::vector<double> reported{ model.Fraction() };
    for (std::size_t stage = 0U; stage < kEditorStartupStageCount; ++stage) {
        model.Begin(static_cast<EditorStartupStage>(stage));
        reported.push_back(model.Fraction());
        for (std::size_t item = 1U; item <= 120U; ++item) {
            model.SetItems(item, 120U);
            reported.push_back(model.Fraction());
        }
    }
    model.Finish();
    reported.push_back(model.Fraction());
    for (const int dpi : kDpis) {
        const EditorSplashLayout layout = EditorSplashPainter::Layout(dpi);
        const int trackWidth = static_cast<int>(layout.track.right - layout.track.left);
        if (EditorSplashPainter::FillWidth(layout, 0.0) != 0 || EditorSplashPainter::FillWidth(layout, 1.0) != trackWidth) {
            fail("the bar is not empty at 0 and full at 1 at " + std::to_string(dpi) + " DPI");
        }
        for (const std::vector<double>* sequence : { &fractions, &reported }) {
            int last = 0;
            for (const double fraction : *sequence) {
                const int width = EditorSplashPainter::FillWidth(layout, fraction);
                if (width < last || width > trackWidth) {
                    fail("the bar fill shrinks or overflows at " + std::to_string(dpi) + " DPI");
                    break;
                }
                last = width;
            }
        }
    }

    // The captures to look at: empty, about half way with a long scene name, done - and the half way one at 200 %.
    const std::string longestShader = LongestShaderName();
    const EditorStartupSnapshot start = State(0.0, EditorStartupStage::Project, {});
    const EditorStartupSnapshot half = State(0.5, EditorStartupStage::Scene, std::string{ kLongSceneName });
    const EditorStartupSnapshot shaders = State(0.72, EditorStartupStage::Renderer,
        EditorStartupProgress::ShadersText(61U, 120U, longestShader));
    const EditorStartupSnapshot done{ 1.0, EditorStartupProgress::FinishedText(), {} };
    if (!automation.CaptureStartupSplash("startup-splash-0", 96, start) ||
        !automation.CaptureStartupSplash("startup-splash-50", 96, half) ||
        !automation.CaptureStartupSplash("startup-splash-72-shaders", 96, shaders) ||
        !automation.CaptureStartupSplash("startup-splash-100", 96, done) ||
        !automation.CaptureStartupSplash("startup-splash-50-200dpi", 192, half)) {
        fail("a splash capture could not be saved");
    }

    // Text: every stage's worst line fits where it is drawn, in every language, at both scales.
    const std::string language{ EditorLocalization::Current() };
    std::size_t checkedLanguages = 0U;
    for (const EditorLanguage& entry : EditorLocalization::Languages()) {
        std::string error;
        if (!EditorLocalization::SetLanguage(entry.code, EditorLocalization::DefaultCatalogFolder(), error)) {
            fail(std::string{ entry.code } + ": " + error);
            continue;
        }
        ++checkedLanguages;
        const ScratchDc scratch(doubled.client);
        if (scratch.Get() == nullptr) {
            fail("no scratch bitmap to paint the splash into");
            break;
        }
        for (const EditorStartupSnapshot& state : WorstStates(longestShader)) {
            for (const int dpi : kDpis) {
                const EditorSplashLayout layout = EditorSplashPainter::Layout(dpi);
                std::vector<CutText> cut;
                CutTextSink() = &cut;
                EditorSplashPainter::Paint(scratch.Get(), layout, MakeEditorDarkTheme(), state);
                CutTextSink() = nullptr;
                for (const CutText& text : cut) {
                    fail(std::string{ entry.code } + " " + std::to_string(dpi) + " DPI: \"" + text.text + "\" needs " +
                        std::to_string(text.needed) + " px, has " + std::to_string(text.available) + " px");
                }
            }
        }
    }
    std::string restoreError;
    static_cast<void>(EditorLocalization::SetLanguage(language, EditorLocalization::DefaultCatalogFolder(), restoreError));

    if (!problems.empty()) {
        return { false, problems };
    }
    return { true, "startup splash: layout at 96 and 192 DPI, monotonic bar, text fits in " +
        std::to_string(checkedLanguages) + " languages; captures startup-splash-*.bmp" };
}

} // namespace kb::editor

#endif
