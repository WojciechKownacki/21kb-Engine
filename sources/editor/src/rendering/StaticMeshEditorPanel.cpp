#include "rendering/StaticMeshEditorPanel.hpp"

#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

constexpr COLORREF kPanel = RGB(28, 30, 34);
constexpr COLORREF kBar = RGB(24, 26, 30);
constexpr COLORREF kBorder = RGB(53, 57, 64);
constexpr COLORREF kText = RGB(211, 217, 225);
constexpr COLORREF kMuted = RGB(139, 149, 161);
constexpr COLORREF kDisabled = RGB(92, 99, 109);
constexpr COLORREF kButton = RGB(38, 41, 47);
constexpr COLORREF kButtonBorder = RGB(59, 64, 72);
constexpr COLORREF kButtonOff = RGB(31, 34, 39);
constexpr COLORREF kActive = RGB(35, 75, 112);
constexpr COLORREF kActiveBorder = RGB(77, 143, 204);
constexpr COLORREF kField = RGB(23, 25, 28);
constexpr COLORREF kViewport = RGB(18, 20, 24);
constexpr COLORREF kWarning = RGB(245, 191, 94);
constexpr COLORREF kError = RGB(232, 120, 110);

constexpr int kTitleHeight = 32;
constexpr int kStripHeight = 30;
constexpr int kRulerHeight = 64;
constexpr int kPadding = 8;
constexpr int kRowHeight = 24;
constexpr int kCardHeaderHeight = 26;
constexpr int kLabelWidth = 104;
constexpr int kStepperButtonWidth = 24;
constexpr int kScrollbarWidth = 4;
constexpr int kMinimumSideWidth = 240;
constexpr int kMaximumSideWidth = 340;
constexpr float kApproximateGlyphWidth = 6.5F;
constexpr float kLineHeight = 16.0F;
constexpr std::uint32_t kMaxMinTriangles = 100'000'000U;
constexpr std::uint32_t kFirstMinTrianglesStep = 64U;

enum class Action : std::uint8_t {
    None,
    ViewLit,
    ViewNormals,
    Frame,
    PinAuto,
    PinLevel,
    ToggleMesh,
    ToggleMaterials,
    ToggleLod,
    ToggleAdvanced,
    LevelsDown,
    LevelsUp,
    ModeAuto,
    ModeManual,
    MinTrianglesDown,
    MinTrianglesUp,
    Generate,
    KeepSlider,
    DeviationSlider,
    LevelShareSlider,
    LevelDeviationSlider,
};

struct Widget {
    RECT rect{};
    Action action = Action::None;
    std::uint32_t level = 0U;
};

struct Regions {
    RECT title{};
    RECT strip{};
    RECT viewport{};
    RECT ruler{};
    RECT side{};
};

[[nodiscard]] Regions ResolveRegions(const RECT& content) noexcept {
    const int width = std::max(0, static_cast<int>(content.right - content.left));
    const int sideWidth = std::min(std::clamp(width / 3, kMinimumSideWidth, kMaximumSideWidth), width / 2);
    const LONG sideLeft = content.right - sideWidth;
    Regions regions;
    regions.title = RECT{ content.left, content.top, content.right, std::min(content.bottom, content.top + kTitleHeight) };
    const LONG bodyTop = regions.title.bottom;
    regions.strip = RECT{ content.left, bodyTop, sideLeft, std::min(content.bottom, bodyTop + kStripHeight) };
    regions.ruler = RECT{ content.left, std::max(regions.strip.bottom, content.bottom - kRulerHeight), sideLeft, content.bottom };
    regions.viewport = RECT{ content.left, regions.strip.bottom, sideLeft, regions.ruler.top };
    regions.side = RECT{ sideLeft, bodyTop, content.right, content.bottom };
    return regions;
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::string Count(std::uint64_t value) {
    std::string digits = std::to_string(value);
    for (std::size_t at = digits.size(); at > 3U; at -= 3U) digits.insert(at - 3U, ",");
    return digits;
}

[[nodiscard]] std::string Percent(float value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), value < 10.0F ? "%.1f%%" : "%.0f%%", static_cast<double>(value));
    return text;
}

[[nodiscard]] std::string Triangles(std::uint64_t count) {
    return EditorLocalization::Format("staticMeshEditor.triangleCount", "{count} tris", { { "count", Count(count) } });
}

struct SliderRange {
    float minimum = 0.0F;
    float maximum = 100.0F;
    bool logarithmic = false;
};

[[nodiscard]] SliderRange RangeOf(Action action) noexcept {
    switch (action) {
    case Action::KeepSlider: return { 1.0F, 99.0F, false };
    case Action::LevelShareSlider: return { 0.1F, 99.9F, true };
    default: return { 1.0F, 100.0F, false };
    }
}

[[nodiscard]] float SliderPosition(Action action, float value) noexcept {
    const SliderRange range = RangeOf(action);
    const float clamped = std::clamp(value, range.minimum, range.maximum);
    return range.logarithmic ? std::log(clamped / range.minimum) / std::log(range.maximum / range.minimum)
                             : (clamped - range.minimum) / (range.maximum - range.minimum);
}

[[nodiscard]] float SliderValue(Action action, const RECT& track, int x) noexcept {
    const SliderRange range = RangeOf(action);
    const float position = std::clamp(static_cast<float>(x - track.left) / static_cast<float>(std::max<LONG>(1, track.right - track.left)), 0.0F, 1.0F);
    const float value = range.logarithmic ? range.minimum * std::pow(range.maximum / range.minimum, position)
                                          : range.minimum + position * (range.maximum - range.minimum);
    const float rounded = value < 10.0F ? std::round(value * 10.0F) / 10.0F : std::round(value);
    return std::clamp(rounded, range.minimum, range.maximum);
}

void ApplySlider(StaticMeshEditorSession& session, Action action, std::uint32_t level, float value) {
    kb::assets::AssetMeshLodSettings& lod = session.Draft();
    switch (action) {
    case Action::KeepSlider: lod.keepPercent = value; break;
    case Action::DeviationSlider: lod.errorPercent = value; break;
    case Action::LevelShareSlider: lod.levels[level - 1U].trianglePercent = value; break;
    case Action::LevelDeviationSlider: lod.levels[level - 1U].errorPercent = value; break;
    default: return;
    }
    session.ClearNotice();
}

[[nodiscard]] std::uint32_t SteppedMinTriangles(std::uint32_t value, bool up) noexcept {
    if (up) return value == 0U ? kFirstMinTrianglesStep : std::min(kMaxMinTriangles, value * 2U);
    return value <= kFirstMinTrianglesStep ? 0U : value / 2U;
}

// One pass over the window: it lays every part out and, when it has a device context, paints it; the widgets it
// collects are what a click can land on.
class Pass {
public:
    Pass(HDC dc, const RECT& content, const StaticMeshEditorSession& session, std::vector<Widget>* widgets)
        : dc_(dc), session_(session), regions_(ResolveRegions(content)), widgets_(widgets) {}

    // Returns the height of the side panel's content.
    int Run() {
        PaintTitle();
        PaintStrip();
        PaintViewportPlaceholder();
        PaintRuler();
        return PaintSide();
    }

    [[nodiscard]] const Regions& Layout() const noexcept { return regions_; }
    void SetScroll(int scroll) noexcept { scroll_ = scroll; }

private:
    void Fill(const RECT& rect, COLORREF color) const {
        if (dc_ != nullptr) GdiDrawing::FillRectColor(dc_, rect, color);
    }
    void Frame(const RECT& rect, COLORREF fill, COLORREF border) const {
        if (dc_ != nullptr) GdiDrawing::DrawSharpFrame(dc_, rect, fill, border);
    }
    void Text(RECT rect, std::string_view text, COLORREF color, const ScopedFont& font, UINT align = DT_LEFT) const {
        if (dc_ == nullptr || text.empty()) return;
        SelectObject(dc_, font.handle);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, color);
        DrawTextUtf8(dc_, text.data(), static_cast<int>(text.size()), &rect,
            align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void Add(const RECT& rect, Action action, std::uint32_t level = 0U) const {
        if (widgets_ != nullptr) widgets_->push_back(Widget{ rect, action, level });
    }
    void Button(const RECT& rect, std::string_view label, Action action, bool active, bool enabled, std::uint32_t level = 0U) const {
        Frame(rect, !enabled ? kButtonOff : (active ? kActive : kButton), active ? kActiveBorder : kButtonBorder);
        Text(rect, label, enabled ? kText : kDisabled, body_, DT_CENTER);
        if (enabled) Add(rect, action, level);
    }

    void PaintTitle() const {
        const RECT& title = regions_.title;
        Frame(title, kBar, kBorder);
        const std::string name = session_.Title();
        RECT nameRect{ title.left + 10, title.top, title.right - 10, title.bottom };
        Text(nameRect, name, kText, heading_);
        if (dc_ != nullptr && session_.Modified()) {
            SIZE size{};
            SelectObject(dc_, heading_.handle);
            GetTextExtentPoint32Utf8(dc_, name.c_str(), static_cast<int>(name.size()), &size);
            Text(RECT{ title.left + 10 + size.cx + 10, title.top, title.right - 10, title.bottom },
                EditorText("staticMeshEditor.modified", "Modified"), kWarning, body_);
        }
        if (!session_.Building()) return;
        // An indeterminate bar: the build reports no progress, only that it is running.
        const RECT track{ title.right - 150, title.top + 11, title.right - 10, title.top + 17 };
        Fill(track, kField);
        const int travel = (track.right - track.left) - 40;
        const int phase = static_cast<int>(std::fmod(session_.BuildSeconds() * 80.0F, static_cast<float>(travel * 2)));
        const int offset = phase < travel ? phase : travel * 2 - phase;
        Fill(RECT{ track.left + offset, track.top, track.left + offset + 40, track.bottom }, kActiveBorder);
        Text(RECT{ track.left - 140, title.top, track.left - 8, title.bottom },
            EditorText("staticMeshEditor.building", "Building levels..."), kMuted, body_, DT_RIGHT);
    }

    void PaintStrip() const {
        const RECT& strip = regions_.strip;
        Frame(strip, kBar, kBorder);
        const int top = strip.top + 4;
        const int bottom = strip.bottom - 4;
        const bool lit = session_.ViewMode() == StaticMeshViewMode::Lit;
        int left = strip.left + 8;
        const auto place = [&](std::string_view label, int width, Action action, bool active) {
            Button(RECT{ left, top, left + width, bottom }, label, action, active, true);
            left += width + 5;
        };
        Text(RECT{ left, strip.top, left + 36, strip.bottom }, EditorText("staticMeshEditor.view", "View"), kMuted, body_);
        left += 40;
        place(EditorText("staticMeshEditor.viewLit", "Lit"), 56, Action::ViewLit, lit);
        place(EditorText("staticMeshEditor.viewNormals", "Normals"), 72, Action::ViewNormals, !lit);
        place(EditorText("staticMeshEditor.frame", "Frame"), 60, Action::Frame, false);
    }

    void PaintViewportPlaceholder() const {
        Fill(regions_.viewport, kViewport);
        if (session_.PreviewScene() != nullptr) return;
        const std::string_view message = !session_.Problem().empty() ? std::string_view{ session_.Problem() }
            : (session_.Building() ? EditorText("staticMeshEditor.loading", "Loading mesh...")
                                   : EditorText("staticMeshEditor.empty", "Open a mesh asset in Project Files to inspect it."));
        Text(regions_.viewport, message, session_.Problem().empty() ? kMuted : kError, heading_, DT_CENTER);
    }

    void PaintRuler() const {
        const RECT& ruler = regions_.ruler;
        Frame(ruler, kBar, kBorder);
        const std::vector<std::uint64_t>& levels = session_.Summary().levelTriangles;
        if (levels.empty()) return;
        const std::uint32_t cells = static_cast<std::uint32_t>(levels.size()) + 1U;
        const int width = std::max(1, static_cast<int>(ruler.right - ruler.left) - 2 * kPadding);
        const int cellWidth = std::max(72, width / static_cast<int>(cells));
        const std::uint64_t finest = std::max<std::uint64_t>(levels.front(), 1U);
        for (std::uint32_t cell = 0U; cell < cells; ++cell) {
            const RECT rect{ ruler.left + kPadding + static_cast<int>(cell) * cellWidth, ruler.top + 5,
                std::min(ruler.right - kPadding, ruler.left + kPadding + static_cast<int>(cell + 1U) * cellWidth - 4), ruler.bottom - 5 };
            if (rect.right <= rect.left) break;
            const bool automatic = cell == 0U;
            const bool active = automatic ? !session_.Pinned().has_value() : session_.Pinned() == cell - 1U;
            Frame(rect, active ? kActive : kButton, active ? kActiveBorder : kButtonBorder);
            const RECT first{ rect.left + 8, rect.top + 3, rect.right - 6, rect.top + 21 };
            const RECT second{ rect.left + 8, rect.top + 20, rect.right - 6, rect.top + 36 };
            if (automatic) {
                Text(first, EditorText("staticMeshEditor.lodAuto", "Auto"), kText, heading_);
                Text(second, EditorText("staticMeshEditor.lodByDistance", "By distance"), kMuted, body_);
                Add(rect, Action::PinAuto);
                continue;
            }
            const std::uint64_t triangles = levels[cell - 1U];
            Text(first, "LOD" + std::to_string(cell - 1U), kText, heading_);
            Text(second, Triangles(triangles) + "  " + Percent(100.0F * static_cast<float>(triangles) / static_cast<float>(finest)), kMuted, body_);
            const float share = static_cast<float>(triangles) / static_cast<float>(finest);
            Fill(RECT{ rect.left + 8, rect.bottom - 8, rect.right - 6, rect.bottom - 5 }, kField);
            Fill(RECT{ rect.left + 8, rect.bottom - 8, rect.left + 8 + static_cast<int>(share * static_cast<float>(rect.right - rect.left - 14)), rect.bottom - 5 }, kActiveBorder);
            Add(rect, Action::PinLevel, cell - 1U);
        }
    }

    int PaintSide() {
        const RECT& side = regions_.side;
        Fill(side, kPanel);
        left_ = side.left + kPadding;
        right_ = side.right - kPadding - kScrollbarWidth - 2;
        y_ = side.top + kPadding - scroll_;
        if (dc_ != nullptr) {
            SaveDC(dc_);
            IntersectClipRect(dc_, side.left, side.top, side.right, side.bottom);
        }
        MeshCard();
        MaterialsCard();
        LodCard();
        const int contentHeight = y_ + scroll_ + kPadding - side.top;
        if (dc_ != nullptr) {
            const int viewHeight = side.bottom - side.top;
            if (contentHeight > viewHeight) {
                const int thumb = std::max(24, viewHeight * viewHeight / contentHeight);
                const int top = side.top + (viewHeight - thumb) * scroll_ / std::max(1, contentHeight - viewHeight);
                Fill(RECT{ side.right - kScrollbarWidth - 2, top, side.right - 2, top + thumb }, kBorder);
            }
            RestoreDC(dc_, -1);
        }
        return contentHeight;
    }

    [[nodiscard]] RECT NextRow(int height = kRowHeight) {
        const RECT row{ left_, y_, right_, y_ + height };
        y_ += height;
        return row;
    }

    // Opens or closes a card; the header toggles it.
    [[nodiscard]] bool Card(std::string_view title, Action toggle, bool open) {
        const RECT header = NextRow(kCardHeaderHeight);
        Frame(header, kBar, kBorder);
        if (dc_ != nullptr) {
            const int cx = header.left + 12;
            const int cy = (header.top + header.bottom) / 2;
            const POINT arrow[3] = { open ? POINT{ cx - 4, cy - 2 } : POINT{ cx - 2, cy - 4 },
                                     open ? POINT{ cx + 4, cy - 2 } : POINT{ cx - 2, cy + 4 },
                                     open ? POINT{ cx, cy + 3 } : POINT{ cx + 3, cy } };
            SelectObject(dc_, GetStockObject(DC_PEN));
            SelectObject(dc_, GetStockObject(DC_BRUSH));
            SetDCPenColor(dc_, kMuted);
            SetDCBrushColor(dc_, kMuted);
            Polygon(dc_, arrow, 3);
        }
        Text(RECT{ header.left + 24, header.top, header.right - 6, header.bottom }, title, kText, heading_);
        Add(header, toggle);
        return open;
    }

    void EndCard() { y_ += kPadding; }

    void ValueRow(std::string_view label, std::string_view value) {
        const RECT row = NextRow();
        Text(RECT{ row.left + 4, row.top, row.left + kLabelWidth, row.bottom }, label, kMuted, body_);
        Text(RECT{ row.left + kLabelWidth, row.top, row.right - 4, row.bottom }, value, kText, body_);
    }

    void Paragraph(std::string_view text, COLORREF color) {
        const int width = std::max(1, right_ - left_ - 8);
        const int lines = std::max(1, static_cast<int>(std::ceil(static_cast<float>(text.size()) * kApproximateGlyphWidth / static_cast<float>(width))));
        const RECT row = NextRow(static_cast<int>(static_cast<float>(lines) * kLineHeight) + 6);
        if (dc_ == nullptr) return;
        SelectObject(dc_, body_.handle);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, color);
        RECT rect{ row.left + 4, row.top + 3, row.right - 4, row.bottom };
        DrawTextUtf8(dc_, text.data(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }

    void Stepper(std::string_view label, std::string_view value, Action down, Action up, bool canDown, bool canUp) {
        const RECT row = NextRow();
        Text(RECT{ row.left + 4, row.top, row.left + kLabelWidth, row.bottom }, label, kMuted, body_);
        const int controls = row.left + kLabelWidth;
        Button(RECT{ controls, row.top + 2, controls + kStepperButtonWidth, row.bottom - 2 }, "-", down, false, canDown);
        Text(RECT{ controls + kStepperButtonWidth, row.top, controls + kStepperButtonWidth + 76, row.bottom }, value, kText, body_, DT_CENTER);
        Button(RECT{ controls + kStepperButtonWidth + 76, row.top + 2, controls + 2 * kStepperButtonWidth + 76, row.bottom - 2 }, "+", up, false, canUp);
    }

    void Slider(const RECT& area, Action action, std::uint32_t level, float value, bool enabled) const {
        constexpr int valueWidth = 46;
        const RECT track{ area.left, area.top + 3, std::max(area.left + 8, area.right - valueWidth), area.bottom - 3 };
        const int middle = (track.top + track.bottom) / 2;
        const RECT bar{ track.left, middle - 2, track.right, middle + 2 };
        Fill(bar, kField);
        const int knob = bar.left + static_cast<int>(SliderPosition(action, value) * static_cast<float>(bar.right - bar.left));
        Fill(RECT{ bar.left, bar.top, knob, bar.bottom }, enabled ? kActiveBorder : kDisabled);
        Frame(RECT{ knob - 3, track.top + 2, knob + 3, track.bottom - 2 }, enabled ? kText : kDisabled, kButtonBorder);
        Text(RECT{ track.right + 4, area.top, area.right, area.bottom }, Percent(value), enabled ? kText : kDisabled, body_, DT_RIGHT);
        if (enabled) Add(track, action, level);
    }

    void SliderRow(std::string_view label, Action action, float value, bool enabled) {
        const RECT row = NextRow();
        Text(RECT{ row.left + 4, row.top, row.left + kLabelWidth, row.bottom }, label, kMuted, body_);
        Slider(RECT{ row.left + kLabelWidth, row.top, row.right - 4, row.bottom }, action, 0U, value, enabled);
    }

    void MeshCard() {
        const StaticMeshSummary& summary = session_.Summary();
        if (!Card(EditorText("staticMeshEditor.cardMesh", "Mesh"), Action::ToggleMesh, session_.Ui().meshCardOpen)) return;
        if (summary.levelTriangles.empty()) {
            ValueRow(EditorText("staticMeshEditor.rowTriangles", "Triangles"), "-");
        } else {
            ValueRow(EditorText("staticMeshEditor.rowTriangles", "Triangles"), Count(summary.levelTriangles.front()));
            ValueRow(EditorText("staticMeshEditor.rowVertices", "Vertices"), Count(summary.vertices));
            ValueRow(EditorText("staticMeshEditor.rowSections", "Sections"), Count(summary.sections));
            char size[96]{};
            std::snprintf(size, sizeof(size), "%.2f x %.2f x %.2f", static_cast<double>(summary.size[0]),
                static_cast<double>(summary.size[1]), static_cast<double>(summary.size[2]));
            ValueRow(EditorText("staticMeshEditor.rowBounds", "Bounds size"), size);
            ValueRow(EditorText("staticMeshEditor.rowMaterialSlots", "Material slots"), Count(summary.materialSlots.size()));
        }
        EndCard();
    }

    void MaterialsCard() {
        const StaticMeshSummary& summary = session_.Summary();
        if (!Card(EditorText("staticMeshEditor.cardMaterials", "Materials"), Action::ToggleMaterials, session_.Ui().materialsCardOpen)) return;
        if (summary.materialSlots.empty()) {
            ValueRow(EditorText("staticMeshEditor.rowMaterialSlots", "Material slots"), "-");
        }
        for (std::size_t slot = 0U; slot < summary.materialSlots.size(); ++slot) {
            const StaticMeshMaterialSlot& entry = summary.materialSlots[slot];
            const std::string label = entry.name.empty()
                ? EditorLocalization::Format("staticMeshEditor.slot", "Slot {index}", { { "index", std::to_string(slot) } })
                : entry.name;
            ValueRow(label, entry.material.empty() ? std::string{ EditorText("staticMeshEditor.noMaterial", "(none)") } : entry.material);
        }
        EndCard();
    }

    void LodCard() {
        if (!Card(EditorText("staticMeshEditor.cardLodChain", "LOD Chain"), Action::ToggleLod, session_.Ui().lodCardOpen)) return;
        const bool editable = session_.Editable();
        const kb::assets::AssetMeshLodSettings& lod = session_.Draft();
        if (!session_.Problem().empty()) Paragraph(session_.Problem(), editable ? kError : kWarning);
        Stepper(EditorText("staticMeshEditor.levels", "Levels"), std::to_string(lod.levelCount), Action::LevelsDown, Action::LevelsUp,
            editable && lod.levelCount > 1U, editable && lod.levelCount < kb::assets::kMeshLodMaxLevels);
        {
            const RECT row = NextRow();
            Text(RECT{ row.left + 4, row.top, row.left + kLabelWidth, row.bottom }, EditorText("staticMeshEditor.mode", "Mode"), kMuted, body_);
            const int controls = row.left + kLabelWidth;
            Button(RECT{ controls, row.top + 2, controls + 70, row.bottom - 2 }, EditorText("staticMeshEditor.modeAuto", "Auto"), Action::ModeAuto, !lod.manual, editable);
            Button(RECT{ controls + 74, row.top + 2, controls + 144, row.bottom - 2 }, EditorText("staticMeshEditor.modeManual", "Manual"), Action::ModeManual, lod.manual, editable);
        }
        if (lod.manual) {
            Paragraph(EditorText("staticMeshEditor.manualHint", "Manual mode: set every level under Advanced."), kMuted);
        } else {
            SliderRow(EditorText("staticMeshEditor.keeps", "Keeps %"), Action::KeepSlider, lod.keepPercent, editable);
            SliderRow(EditorText("staticMeshEditor.deviation", "Deviation %"), Action::DeviationSlider, lod.errorPercent, editable);
        }
        Stepper(EditorText("staticMeshEditor.minTriangles", "Min triangles"), Count(lod.minTriangles), Action::MinTrianglesDown, Action::MinTrianglesUp,
            editable && lod.minTriangles > 0U, editable && lod.minTriangles < kMaxMinTriangles);
        {
            const RECT row = NextRow(kRowHeight + 4);
            Button(RECT{ row.left + 4, row.top + 2, row.left + 104, row.bottom - 2 }, EditorText("staticMeshEditor.generate", "Generate"), Action::Generate, false, editable);
            Text(RECT{ row.left + 112, row.top, row.right, row.bottom },
                session_.Building() ? EditorText("staticMeshEditor.building", "Building levels...")
                    : (session_.Modified() ? EditorText("staticMeshEditor.modified", "Modified") : EditorText("staticMeshEditor.upToDate", "Up to date")),
                session_.Modified() && !session_.Building() ? kWarning : kMuted, body_);
        }
        const std::string reason = !session_.Notice().empty() ? session_.Notice()
            : (editable ? StaticMeshEditorSession::Validate(lod) : std::string{});
        if (!reason.empty()) Paragraph(reason, kError);
        {
            const RECT row = NextRow();
            Frame(row, kBar, kBorder);
            Text(RECT{ row.left + 8, row.top, row.right - 4, row.bottom }, EditorText("staticMeshEditor.advanced", "Advanced"), kText, body_);
            Text(RECT{ row.left + 8, row.top, row.right - 8, row.bottom }, session_.Ui().advancedOpen ? "-" : "+", kMuted, body_, DT_RIGHT);
            Add(row, Action::ToggleAdvanced);
        }
        if (session_.Ui().advancedOpen) {
            const bool edit = editable && lod.manual;
            for (std::uint32_t level = 1U; level < lod.levelCount; ++level) {
                const RECT row = NextRow();
                const kb::assets::AssetMeshLodLevel entry = lod.Level(level);
                Text(RECT{ row.left + 4, row.top, row.left + 44, row.bottom }, "LOD" + std::to_string(level), kMuted, body_);
                const int middle = (row.left + 44 + row.right) / 2;
                Slider(RECT{ row.left + 44, row.top, middle - 4, row.bottom }, Action::LevelShareSlider, level, entry.trianglePercent, edit);
                Slider(RECT{ middle + 4, row.top, row.right - 4, row.bottom }, Action::LevelDeviationSlider, level, entry.errorPercent, edit);
            }
            if (lod.levelCount > 1U) {
                const RECT row = NextRow(kRowHeight - 6);
                const int middle = (row.left + 44 + row.right) / 2;
                Text(RECT{ row.left + 44, row.top, middle - 4, row.bottom }, EditorText("staticMeshEditor.advancedTriangles", "Triangles %"), kMuted, body_);
                Text(RECT{ middle + 4, row.top, row.right - 4, row.bottom }, EditorText("staticMeshEditor.advancedDeviation", "Deviation %"), kMuted, body_);
            }
        }
        EndCard();
    }

    HDC dc_ = nullptr;
    const StaticMeshEditorSession& session_;
    Regions regions_;
    std::vector<Widget>* widgets_ = nullptr;
    int scroll_ = 0;
    int left_ = 0;
    int right_ = 0;
    int y_ = 0;
    const ScopedFont body_{ 11, FW_NORMAL };
    const ScopedFont heading_{ 12, FW_SEMIBOLD };
};

// The widgets a click can land on, with the side panel scrolled where the session keeps it.
struct Collected {
    std::vector<Widget> widgets;
    Regions regions;
    int contentHeight = 0;
};

[[nodiscard]] Collected Collect(const RECT& content, const StaticMeshEditorSession& session, int scroll) {
    Collected collected;
    Pass pass(nullptr, content, session, &collected.widgets);
    pass.SetScroll(scroll);
    collected.contentHeight = pass.Run();
    collected.regions = pass.Layout();
    return collected;
}

// How far the side panel can scroll: its content less what fits.
[[nodiscard]] int MaxScroll(const RECT& content, const StaticMeshEditorSession& session) {
    const Collected unscrolled = Collect(content, session, 0);
    return std::max(0, unscrolled.contentHeight - static_cast<int>(unscrolled.regions.side.bottom - unscrolled.regions.side.top));
}

[[nodiscard]] Collected CollectScrolled(const RECT& content, const StaticMeshEditorSession& session) {
    return Collect(content, session, std::clamp(session.Ui().scroll, 0, MaxScroll(content, session)));
}

[[nodiscard]] const Widget* HitWidget(const Collected& collected, int x, int y) noexcept {
    const bool inSide = Contains(collected.regions.side, x, y);
    for (auto widget = collected.widgets.rbegin(); widget != collected.widgets.rend(); ++widget) {
        if (!Contains(widget->rect, x, y)) continue;
        // Cards scroll under the title and the strip; a widget half out of the side panel is hit only inside it.
        const bool sideWidget = widget->rect.left >= collected.regions.side.left;
        if (!sideWidget || inSide) return &*widget;
    }
    return nullptr;
}

[[nodiscard]] bool IsSlider(Action action) noexcept {
    return action == Action::KeepSlider || action == Action::DeviationSlider ||
        action == Action::LevelShareSlider || action == Action::LevelDeviationSlider;
}

} // namespace

void StaticMeshEditorPanel::Paint(HDC dc, const RECT& content, const StaticMeshEditorSession& session) {
    const int saved = SaveDC(dc);
    Pass pass(dc, content, session, nullptr);
    pass.SetScroll(std::clamp(session.Ui().scroll, 0, MaxScroll(content, session)));
    static_cast<void>(pass.Run());
    RestoreDC(dc, saved);
}

RECT StaticMeshEditorPanel::ViewportRect(const RECT& content) noexcept {
    return ResolveRegions(content).viewport;
}

bool StaticMeshEditorPanel::PointerDown(
    const RECT& content, int x, int y, StaticMeshEditorSession& session, kb::assets::AssetManager& manager) {
    const Collected collected = CollectScrolled(content, session);
    const Widget* widget = HitWidget(collected, x, y);
    if (widget == nullptr) return false;
    StaticMeshEditorUiState& ui = session.Ui();
    kb::assets::AssetMeshLodSettings& lod = session.Draft();
    switch (widget->action) {
    case Action::ViewLit: session.SetViewMode(StaticMeshViewMode::Lit); break;
    case Action::ViewNormals: session.SetViewMode(StaticMeshViewMode::Normals); break;
    case Action::Frame: session.Frame(); break;
    case Action::PinAuto: session.SetPinned(std::nullopt); break;
    case Action::PinLevel: session.SetPinned(widget->level); break;
    case Action::ToggleMesh: ui.meshCardOpen = !ui.meshCardOpen; break;
    case Action::ToggleMaterials: ui.materialsCardOpen = !ui.materialsCardOpen; break;
    case Action::ToggleLod: ui.lodCardOpen = !ui.lodCardOpen; break;
    case Action::ToggleAdvanced: ui.advancedOpen = !ui.advancedOpen; break;
    case Action::LevelsDown: session.SetLevelCount(static_cast<std::uint8_t>(lod.levelCount - 1U)); break;
    case Action::LevelsUp: session.SetLevelCount(static_cast<std::uint8_t>(lod.levelCount + 1U)); break;
    case Action::ModeAuto: session.SetManual(false); break;
    case Action::ModeManual: session.SetManual(true); break;
    case Action::MinTrianglesDown: lod.minTriangles = SteppedMinTriangles(lod.minTriangles, false); session.ClearNotice(); break;
    case Action::MinTrianglesUp: lod.minTriangles = SteppedMinTriangles(lod.minTriangles, true); session.ClearNotice(); break;
    case Action::Generate: static_cast<void>(session.Generate(manager)); break;
    default:
        if (!IsSlider(widget->action)) return false;
        ui.dragAction = static_cast<std::uint8_t>(widget->action);
        ui.dragLevel = widget->level;
        ApplySlider(session, widget->action, widget->level, SliderValue(widget->action, widget->rect, x));
        break;
    }
    return true;
}

bool StaticMeshEditorPanel::PointerMove(const RECT& content, int x, int y, StaticMeshEditorSession& session) {
    static_cast<void>(y);
    const StaticMeshEditorUiState& ui = session.Ui();
    if (ui.dragAction == 0U) return false;
    const Collected collected = CollectScrolled(content, session);
    const auto action = static_cast<Action>(ui.dragAction);
    const auto dragged = std::ranges::find_if(collected.widgets, [&](const Widget& widget) {
        return widget.action == action && widget.level == ui.dragLevel;
    });
    if (dragged == collected.widgets.end()) return false;
    ApplySlider(session, action, ui.dragLevel, SliderValue(action, dragged->rect, x));
    return true;
}

bool StaticMeshEditorPanel::PointerUp(StaticMeshEditorSession& session) noexcept {
    return std::exchange(session.Ui().dragAction, std::uint8_t{ 0U }) != 0U;
}

bool StaticMeshEditorPanel::Wheel(const RECT& content, int x, int y, float wheelSteps, StaticMeshEditorSession& session) {
    const Regions regions = ResolveRegions(content);
    if (!Contains(regions.side, x, y)) return false;
    StaticMeshEditorUiState& ui = session.Ui();
    ui.scroll = std::clamp(ui.scroll - static_cast<int>(wheelSteps * 3.0F * kRowHeight), 0, MaxScroll(content, session));
    return true;
}

} // namespace kb::editor
#endif
