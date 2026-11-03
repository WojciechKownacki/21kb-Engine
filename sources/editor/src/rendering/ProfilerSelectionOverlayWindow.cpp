#include "rendering/ProfilerSelectionOverlayWindow.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "profiler/ProfilerOverlayLayout.hpp"
#include "profiler/ProfilerPalette.hpp"
#include "profiler/ProfilerText.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string>
#include <vector>
#include <windowsx.h>

namespace kb::editor {
namespace {

using kb::core::ProfilerArea;

constexpr wchar_t kClassName[] = L"KBEditorProfilerSelectionOverlay";
constexpr int kTitleHeight = EditorDialogStyle::TitleBarHeight;
constexpr int kStripHeight = 52;
constexpr int kHeaderHeight = 30;
constexpr int kRowHeight = 24;
constexpr int kFooterHeight = 46;
constexpr int kAreasWidth = 300;
constexpr int kCountersWidth = 400;
constexpr int kCellWidth = 54;
constexpr int kBox = 14;
constexpr int kPreviewHeight = 240;
constexpr int kOptionRow = 28;
constexpr int kFieldWidth = 168;
constexpr int kPad = 12;

constexpr std::array<ProfilerArea, 5> kAreas{
    ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };

[[nodiscard]] COLORREF C(EditorColor color) noexcept { return EditorDialogStyle::Color(color); }

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT Inset(RECT rect, int dx, int dy) noexcept {
    return RECT{ rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy };
}

template <typename Enum>
[[nodiscard]] Enum Next(Enum value, int count) noexcept {
    return static_cast<Enum>((static_cast<int>(value) + 1) % count);
}

[[nodiscard]] std::string CornerText(ProfilerCorner corner) {
    switch (corner) {
    case ProfilerCorner::TopLeft: return std::string{ EditorText("profiler.corner.top_left", "top left corner") };
    case ProfilerCorner::TopRight: return std::string{ EditorText("profiler.corner.top_right", "top right corner") };
    case ProfilerCorner::BottomLeft: return std::string{ EditorText("profiler.corner.bottom_left", "bottom left corner") };
    case ProfilerCorner::BottomRight: return std::string{ EditorText("profiler.corner.bottom_right", "bottom right corner") };
    }
    return {};
}

[[nodiscard]] std::string TextSizeText(ProfilerTextSize size) {
    switch (size) {
    case ProfilerTextSize::Small: return std::string{ EditorText("profiler.size.small", "small") };
    case ProfilerTextSize::Medium: return std::string{ EditorText("profiler.size.medium", "medium") };
    case ProfilerTextSize::Large: return std::string{ EditorText("profiler.size.large", "large") };
    }
    return {};
}

[[nodiscard]] std::string RefreshText(int perSecond) {
    if (perSecond == 0) return std::string{ EditorText("profiler.refresh.frame", "every frame") };
    return EditorLocalization::Plural("profiler.refresh.rate", perSecond, "{count} time a second", "{count} times a second");
}

// The order the window cycles a choice through, from the value it has.
template <std::size_t N>
[[nodiscard]] std::uint8_t NextChoice(const std::array<std::uint8_t, N>& choices, std::uint8_t current) noexcept {
    const auto found = std::ranges::find(choices, current);
    return found == choices.end() || std::next(found) == choices.end() ? choices.front() : *std::next(found);
}

[[nodiscard]] bool SameSet(std::vector<std::string> first, std::vector<std::string> second) {
    std::ranges::sort(first);
    std::ranges::sort(second);
    return first == second;
}

[[nodiscard]] bool MatchesPreset(const ProfilerSelection& selection, ProfilerPreset preset, const kb::core::ProfilerRegistry& registry) {
    ProfilerSelection expected;
    expected.ApplyPreset(preset, registry);
    return SameSet(selection.areas, expected.areas) && SameSet(selection.panelCounters, expected.panelCounters)
        && SameSet(selection.sceneCounters, expected.sceneCounters);
}

} // namespace

struct ProfilerSelectionOverlayWindow::Item {
    enum class Kind : std::uint8_t {
        Close, Done, Restore, Preset, AreasAll, AreasNone, CountersAll, CountersNone,
        Area, PanelCounter, SceneCounter,
        ListVisible, ListCorner, GraphVisible, GraphCorner, TextSize, Refresh, Opacity, Line,
        GroupHeader, CounterName,
    };
    Kind kind = Kind::Close;
    int argument = 0;
    RECT bounds{};
    std::string text;
    std::string detail;       // the cost of an area, or the value shown in a field
    EditorColor swatch{};
    bool hasSwatch = false;
    bool checked = false;
    bool enabled = true;

    [[nodiscard]] bool Interactive() const noexcept { return kind != Kind::GroupHeader && kind != Kind::CounterName && enabled; }
};

struct ProfilerSelectionOverlayWindow::Layout {
    RECT title{}, strip{}, footer{};
    RECT areasColumn{}, countersColumn{}, sceneColumn{};
    RECT areasHint{}, countersCaption{}, areasList{}, countersList{};
    RECT preview{}, appearanceHeader{}, linesHeader{};
    RECT panelCaption{}, sceneCaption{};
    int countersContent = 0;
    std::vector<Item> items;
};

ProfilerSelectionOverlayWindow::ProfilerSelectionOverlayWindow() noexcept
    : popup_(kClassName, *this) {}

void ProfilerSelectionOverlayWindow::Show(HWND parent, const EditorTheme& theme, EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport) {
    if (parent == nullptr || !sceneContext.Profiler().WindowOpen() || !popup_.Ensure(parent)) {
        Hide();
        return;
    }
    theme_ = theme;
    sceneContext_ = &sceneContext;
    sceneViewport_ = &sceneViewport;
    const RECT bounds = ResolveScreenBounds();
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        Hide();
        return;
    }
    // Painted on every main paint as well: the preview shows live numbers. It is raised too, above
    // the scene's own overlay, which may have been shown after it.
    popup_.ShowAt(bounds);
    SetWindowPos(popup_.Window(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    popup_.Invalidate();
}

void ProfilerSelectionOverlayWindow::Hide() noexcept {
    popup_.Hide();
    hovered_ = -1;
    // The preview's fonts are GDI+ objects, which must not outlive the program's GDI+ session.
    previewRenderer_.Destroy();
}

void ProfilerSelectionOverlayWindow::PaintInto(HDC dc, const RECT& client, const EditorTheme& theme, EditorSceneContext& sceneContext) {
    theme_ = theme;
    sceneContext_ = &sceneContext;
    PaintOverlay(dc, client);
}

bool ProfilerSelectionOverlayWindow::ClickAt(const RECT& client, int x, int y, const EditorTheme& theme,
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) {
    theme_ = theme;
    sceneContext_ = &sceneContext;
    sceneViewport_ = &sceneViewport;
    const Layout layout = BuildLayout(client);
    const int index = ItemAt(layout, x, y);
    if (index < 0) return false;
    Activate(layout.items[static_cast<std::size_t>(index)]);
    return true;
}

RECT ProfilerSelectionOverlayWindow::ResolveScreenBounds() const noexcept {
    const HWND owner = popup_.Owner();
    if (owner == nullptr || IsWindow(owner) == 0) return {};
    RECT client{};
    GetClientRect(owner, &client);
    const int width = std::min<int>(1180, client.right - client.left - 40);
    const int height = std::min<int>(740, client.bottom - client.top - 40);
    if (width < kAreasWidth + kCountersWidth || height < 360) return {};
    const int left = client.left + (client.right - client.left - width) / 2;
    const int top = client.top + (client.bottom - client.top - height) / 2;
    return EditorOverlayPopupWindow::OwnerClientToScreen(owner, RECT{ left, top, left + width, top + height });
}

ProfilerSelectionOverlayWindow::Layout ProfilerSelectionOverlayWindow::BuildLayout(const RECT& client) const {
    using Kind = Item::Kind;
    const ProfilerSelection& selection = sceneContext_->EditorConfig().profiler;
    const kb::core::ProfilerRegistry& registry = sceneContext_->Profiler().Registry();
    const ProfilerOverlayOptions& options = selection.overlay;
    Layout layout;
    const int width = client.right - client.left;
    const int bodyTop = kTitleHeight + kStripHeight;
    const int bodyBottom = client.bottom - kFooterHeight;
    layout.title = RECT{ 0, 0, width, kTitleHeight };
    layout.strip = RECT{ 0, kTitleHeight, width, bodyTop };
    layout.footer = RECT{ 0, bodyBottom, width, client.bottom };
    layout.areasColumn = RECT{ 0, bodyTop, kAreasWidth, bodyBottom };
    layout.countersColumn = RECT{ kAreasWidth, bodyTop, kAreasWidth + kCountersWidth, bodyBottom };
    layout.sceneColumn = RECT{ kAreasWidth + kCountersWidth, bodyTop, width, bodyBottom };

    const auto add = [&layout](Item item) { layout.items.push_back(std::move(item)); };
    add({ .kind = Kind::Close, .bounds = RECT{ width - 34, 0, width, kTitleHeight } });

    // Presets: the one the choice equals is lit.
    const std::array<std::pair<ProfilerPreset, std::string_view>, 4> presets{{
        { ProfilerPreset::Minimal, EditorText("profiler.preset.minimal", "Minimal") },
        { ProfilerPreset::Rendering, EditorText("profiler.preset.rendering", "Rendering") },
        { ProfilerPreset::Memory, EditorText("profiler.preset.memory", "Memory") },
        { ProfilerPreset::Everything, EditorText("profiler.preset.everything", "Everything") },
    }};
    int pillLeft = 96;
    for (const auto& [preset, name] : presets) {
        add({ .kind = Kind::Preset, .argument = static_cast<int>(preset),
              .bounds = RECT{ pillLeft, kTitleHeight + 13, pillLeft + 112, kTitleHeight + 39 },
              .text = std::string{ name }, .checked = MatchesPreset(selection, preset, registry) });
        pillLeft += 120;
    }

    // Column headers carry the "all / none" links.
    const auto links = [&](const RECT& column, Kind all, Kind none) {
        const int top = column.top + 6;
        add({ .kind = none, .bounds = RECT{ column.right - kPad - 38, top, column.right - kPad, top + 18 },
              .text = std::string{ EditorText("profiler.select.none", "none") } });
        add({ .kind = all, .bounds = RECT{ column.right - kPad - 38 - 14 - 28, top, column.right - kPad - 38 - 14, top + 18 },
              .text = std::string{ EditorText("profiler.select.all", "all") } });
    };
    links(layout.areasColumn, Kind::AreasAll, Kind::AreasNone);
    links(layout.countersColumn, Kind::CountersAll, Kind::CountersNone);

    // Areas, grouped like the engine's own areas of work.
    layout.areasHint = RECT{ 0, bodyTop + kHeaderHeight, kAreasWidth, bodyTop + kHeaderHeight + 52 };
    layout.areasList = RECT{ 0, layout.areasHint.bottom, kAreasWidth, bodyBottom };
    struct AreaGroup { std::string_view name; std::array<ProfilerArea, 2> areas; int count; };
    const std::array<AreaGroup, 3> groups{{
        { EditorText("profiler.areagroup.threads", "Threads"), { ProfilerArea::Cpu, ProfilerArea::Gpu }, 2 },
        { EditorText("profiler.areagroup.systems", "Systems"), { ProfilerArea::Render, ProfilerArea::Ecs }, 2 },
        { EditorText("profiler.areagroup.resources", "Resources"), { ProfilerArea::Memory, ProfilerArea::Memory }, 1 },
    }};
    int y = layout.areasList.top;
    for (const AreaGroup& group : groups) {
        add({ .kind = Kind::GroupHeader, .bounds = RECT{ 0, y, kAreasWidth, y + kRowHeight }, .text = std::string{ group.name } });
        y += kRowHeight;
        for (int index = 0; index < group.count; ++index) {
            const ProfilerArea area = group.areas[static_cast<std::size_t>(index)];
            add({ .kind = Kind::Area, .argument = static_cast<int>(area), .bounds = RECT{ 0, y, kAreasWidth, y + kRowHeight },
                  .text = std::string{ ProfilerText::Area(area) },
                  .detail = std::string{ area == ProfilerArea::Ecs ? EditorText("profiler.cost.medium", "medium cost") : EditorText("profiler.cost.low", "low cost") },
                  .swatch = profiler_palette::Area(area), .hasSwatch = true, .checked = selection.AreaMeasured(area) });
            y += kRowHeight;
        }
    }

    // Counters: the two checkbox columns are the panel's list and the scene's list.
    layout.countersCaption = RECT{ layout.countersColumn.left, bodyTop + kHeaderHeight, layout.countersColumn.right, bodyTop + kHeaderHeight + 22 };
    layout.countersList = RECT{ layout.countersColumn.left, layout.countersCaption.bottom, layout.countersColumn.right, bodyBottom };
    layout.sceneCaption = RECT{ layout.countersColumn.right - kCellWidth, layout.countersCaption.top, layout.countersColumn.right, layout.countersCaption.bottom };
    layout.panelCaption = RECT{ layout.sceneCaption.left - kCellWidth, layout.countersCaption.top, layout.sceneCaption.left, layout.countersCaption.bottom };
    std::string_view lastGroup;
    int content = 0;
    for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
        const kb::core::ProfilerCounterInfo& info = registry.Info(counter);
        if (info.group != lastGroup) {
            lastGroup = info.group;
            const int top = layout.countersList.top + content - counterScroll_;
            add({ .kind = Kind::GroupHeader, .bounds = RECT{ layout.countersList.left, top, layout.countersList.right, top + kRowHeight },
                  .text = std::string{ ProfilerText::Group(info.group) } });
            content += kRowHeight;
        }
        const int top = layout.countersList.top + content - counterScroll_;
        const RECT row{ layout.countersList.left, top, layout.countersList.right, top + kRowHeight };
        const bool measured = selection.AreaMeasured(info.area);
        add({ .kind = Kind::CounterName, .bounds = row, .text = std::string{ ProfilerText::Counter(info) }, .enabled = measured });
        add({ .kind = Kind::PanelCounter, .argument = static_cast<int>(counter),
              .bounds = RECT{ layout.panelCaption.left, top, layout.panelCaption.right, top + kRowHeight },
              .checked = selection.InPanel(info.id), .enabled = measured });
        add({ .kind = Kind::SceneCounter, .argument = static_cast<int>(counter),
              .bounds = RECT{ layout.sceneCaption.left, top, layout.sceneCaption.right, top + kRowHeight },
              .checked = selection.OnScene(info.id), .enabled = measured });
        content += kRowHeight;
    }
    layout.countersContent = content;

    // The scene: a preview, how it looks and which lines the graph draws.
    const int sceneLeft = layout.sceneColumn.left + kPad;
    const int sceneRight = layout.sceneColumn.right - kPad;
    layout.preview = RECT{ sceneLeft, bodyTop + kHeaderHeight + 10, sceneRight, bodyTop + kHeaderHeight + 10 + kPreviewHeight };
    layout.appearanceHeader = RECT{ layout.sceneColumn.left, layout.preview.bottom + 10, layout.sceneColumn.right, layout.preview.bottom + 10 + kHeaderHeight };
    int rowTop = layout.appearanceHeader.bottom + 10;
    const auto option = [&](Kind check, Kind field, std::string_view label, std::string value, bool checkable, bool checked) {
        const RECT row{ sceneLeft, rowTop, sceneRight, rowTop + kOptionRow };
        const RECT fieldRect{ sceneRight - kFieldWidth, rowTop + 2, sceneRight, rowTop + kOptionRow - 2 };
        if (checkable) {
            add({ .kind = check, .bounds = RECT{ row.left, row.top, fieldRect.left - 8, row.bottom }, .text = std::string{ label }, .checked = checked });
        } else {
            add({ .kind = Kind::CounterName, .bounds = RECT{ row.left, row.top, fieldRect.left - 8, row.bottom }, .text = std::string{ label } });
        }
        add({ .kind = field, .bounds = fieldRect, .detail = std::move(value) });
        rowTop += kOptionRow;
    };
    option(Kind::ListVisible, Kind::ListCorner, EditorText("profiler.option.list", "Counter list on the scene"), CornerText(options.listCorner), true, options.listVisible);
    option(Kind::GraphVisible, Kind::GraphCorner, EditorText("profiler.option.graph", "Live graph on the scene"), CornerText(options.graphCorner), true, options.graphVisible);
    option(Kind::CounterName, Kind::TextSize, EditorText("profiler.option.text_size", "Text size"), TextSizeText(options.textSize), false, false);
    option(Kind::CounterName, Kind::Refresh, EditorText("profiler.option.refresh", "Number refresh"), RefreshText(options.refreshPerSecond), false, false);
    option(Kind::CounterName, Kind::Opacity, EditorText("profiler.option.opacity", "Background opacity"), std::format("{}%", options.opacityPercent), false, false);

    layout.linesHeader = RECT{ layout.sceneColumn.left, rowTop + 10, layout.sceneColumn.right, rowTop + 10 + kHeaderHeight };
    struct LineChoice { int id; std::string_view label; EditorColor color; bool checked; };
    const std::array<LineChoice, 5> lines{{
        { 0, EditorText("profiler.line.frame", "Frame"), kb::editor::EditorColor{ 0, 0, 0 }, options.lines.frame },
        { 1, EditorText("profiler.line.render", "Render thread"), profiler_palette::kGreen, options.lines.renderThread },
        { 2, EditorText("profiler.line.gpu", "GPU"), profiler_palette::kWarning, options.lines.gpu },
        { 3, EditorText("profiler.lines.budget", "Budget line"), profiler_palette::kBudget, options.lines.budget },
        { 4, EditorText("profiler.lines.alarm", "Alarm line"), profiler_palette::kAlarm, options.lines.alarm },
    }};
    const int columnWidth = (sceneRight - sceneLeft) / 2;
    for (const LineChoice& line : lines) {
        const int left = sceneLeft + (line.id % 2) * columnWidth;
        const int top = layout.linesHeader.bottom + 10 + (line.id / 2) * kOptionRow;
        add({ .kind = Kind::Line, .argument = line.id, .bounds = RECT{ left, top, left + columnWidth - 8, top + kOptionRow },
              .text = std::string{ line.label }, .swatch = line.id == 0 ? theme_.textPrimary : line.color, .hasSwatch = true, .checked = line.checked });
    }

    add({ .kind = Kind::Restore, .bounds = RECT{ width - kPad - 90 - 8 - 160, bodyBottom + 10, width - kPad - 90 - 8, bodyBottom + 36 },
          .text = std::string{ EditorText("profiler.button.restore", "Restore defaults") } });
    add({ .kind = Kind::Done, .bounds = RECT{ width - kPad - 90, bodyBottom + 10, width - kPad, bodyBottom + 36 },
          .text = std::string{ EditorText("profiler.button.done", "Done") } });
    return layout;
}

int ProfilerSelectionOverlayWindow::ItemAt(const Layout& layout, int x, int y) const {
    for (std::size_t index = layout.items.size(); index-- > 0U;) {
        const Item& item = layout.items[index];
        if (!item.Interactive() || !Contains(item.bounds, x, y)) continue;
        // Rows scrolled out of the counters list are not there to be hit.
        const bool inCounters = item.kind == Item::Kind::PanelCounter || item.kind == Item::Kind::SceneCounter;
        if (inCounters && !Contains(layout.countersList, x, y)) continue;
        return static_cast<int>(index);
    }
    return -1;
}

void ProfilerSelectionOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (sceneContext_ == nullptr) return;
    using Kind = Item::Kind;
    const Layout layout = BuildLayout(client);
    const ProfilerSelection& selection = sceneContext_->EditorConfig().profiler;
    const EditorTheme& theme = theme_;

    EditorDialogStyle::PaintSurface(dc, client, theme);
    EditorDialogStyle::PaintFooter(dc, layout.footer, theme);
    EditorDialogStyle::PaintTitleBar(dc, theme, EditorDialogHeaderDescriptor{
        .bounds = client, .closeButton = layout.items.front().bounds,
        .title = EditorText("profiler.window.title", "What to show"), .icon = HeroIconKind::AdjustmentsHorizontal,
        .showIcon = true, .closeHovered = hovered_ == 0 });

    // Presets strip.
    EditorDialogStyle::PaintToolbar(dc, layout.strip, theme);
    EditorDialogStyle::PaintText(dc, RECT{ kPad + 2, layout.strip.top, 90, layout.strip.bottom },
        EditorText("profiler.preset.label", "Preset"), C(theme.textSecondary), 11);

    // Column bodies and headers.
    const auto header = [&](const RECT& column, int number, std::string_view title, bool links) {
        const RECT bar{ column.left, column.top, column.right, column.top + kHeaderHeight };
        GdiDrawing::FillRectColor(dc, bar, C(theme.strip));
        const RECT badge{ column.left + kPad, bar.top + 6, column.left + kPad + 18, bar.top + 24 };
        EditorDialogStyle::PaintBadge(dc, badge, theme, std::to_string(number));
        EditorDialogStyle::PaintText(dc, RECT{ badge.right + 8, bar.top, bar.right - 100, bar.bottom }, title, C(theme.textPrimary), 12, FW_SEMIBOLD);
        // The dot between the two links of the header, which are items of their own.
        if (links) {
            EditorDialogStyle::PaintText(dc, RECT{ bar.right - kPad - 38 - 14, bar.top, bar.right - kPad - 38, bar.bottom }, "\xC2\xB7",
                C(theme.textDisabled), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    GdiDrawing::FillRectColor(dc, RECT{ layout.areasColumn.left, layout.areasColumn.top + kHeaderHeight, layout.areasColumn.right, layout.areasColumn.bottom }, C(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{ layout.countersColumn.left, layout.countersColumn.top + kHeaderHeight, layout.countersColumn.right, layout.countersColumn.bottom }, C(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{ layout.sceneColumn.left, layout.sceneColumn.top + kHeaderHeight, layout.sceneColumn.right, layout.sceneColumn.bottom }, C(theme.panel));
    header(layout.areasColumn, 1, EditorText("profiler.areas.title", "Areas - what to measure"), true);
    header(layout.countersColumn, 2, EditorText("profiler.counters.title", "Counters - what to show"), true);
    header(layout.sceneColumn, 3, EditorText("profiler.preview.title", "Scene preview"), false);
    for (const RECT& column : { layout.areasColumn, layout.countersColumn }) {
        EditorDialogStyle::PaintDivider(dc, RECT{ column.right - 1, column.top, column.right, column.bottom }, theme);
    }
    EditorDialogStyle::PaintText(dc, Inset(layout.areasHint, kPad, 6),
        EditorText("profiler.areas.hint", "Checked areas are measured and appear in the window. Unchecked ones cost nothing."),
        C(theme.textDisabled), 10, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
    EditorDialogStyle::PaintDivider(dc, RECT{ layout.areasHint.left, layout.areasHint.bottom - 1, layout.areasHint.right, layout.areasHint.bottom }, theme);
    GdiDrawing::FillRectColor(dc, layout.countersCaption, C(theme.strip));
    EditorDialogStyle::PaintText(dc, RECT{ layout.countersCaption.left + kPad, layout.countersCaption.top, layout.panelCaption.left, layout.countersCaption.bottom },
        EditorText("profiler.counters.caption", "Counter"), C(theme.textDisabled), 10);
    EditorDialogStyle::PaintText(dc, layout.panelCaption, EditorText("profiler.column.panel", "in panel"), C(theme.textDisabled), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EditorDialogStyle::PaintText(dc, layout.sceneCaption, EditorText("profiler.column.scene", "on scene"), C(theme.textDisabled), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EditorDialogStyle::PaintDivider(dc, RECT{ layout.countersCaption.left, layout.countersCaption.bottom - 1, layout.countersCaption.right, layout.countersCaption.bottom }, theme);

    const auto fieldChevron = [&](const RECT& field) {
        HeroIconPainter::Draw(dc, RECT{ field.right - 20, field.top + (field.bottom - field.top - 10) / 2, field.right - 10, field.top + (field.bottom - field.top + 10) / 2 },
            HeroIconKind::ChevronDown, C(theme.textSecondary), 1);
    };

    for (std::size_t index = 0U; index < layout.items.size(); ++index) {
        const Item& item = layout.items[index];
        const bool hot = static_cast<int>(index) == hovered_ && item.Interactive();
        // The counters scroll, so the rows of that list are clipped to it.
        const bool inCounters = item.bounds.left >= layout.countersList.left && item.bounds.right <= layout.countersList.right
            && (item.kind == Kind::GroupHeader || item.kind == Kind::CounterName || item.kind == Kind::PanelCounter || item.kind == Kind::SceneCounter);
        if (inCounters && (item.bounds.bottom <= layout.countersList.top || item.bounds.top >= layout.countersList.bottom)) continue;
        const int savedDc = SaveDC(dc);
        if (inCounters) {
            IntersectClipRect(dc, layout.countersList.left, layout.countersList.top, layout.countersList.right, layout.countersList.bottom);
        }
        switch (item.kind) {
        case Kind::Close: break;  // painted with the title bar
        case Kind::Preset:
            EditorDialogStyle::PaintOptionButton(dc, item.bounds, theme, item.text, item.checked);
            if (hot && !item.checked) EditorDialogStyle::PaintButton(dc, item.bounds, theme, item.text, EditorDialogButtonTone::Neutral, true);
            break;
        case Kind::AreasAll: case Kind::AreasNone: case Kind::CountersAll: case Kind::CountersNone:
            EditorDialogStyle::PaintText(dc, item.bounds, item.text, C(hot ? theme.textPrimary : theme.textDisabled), 10, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            break;
        case Kind::GroupHeader:
            GdiDrawing::FillRectColor(dc, item.bounds, C(theme.strip));
            EditorDialogStyle::PaintText(dc, Inset(item.bounds, kPad, 0), item.text, C(theme.textDisabled), 10, FW_SEMIBOLD);
            break;
        case Kind::Area: {
            if (hot) GdiDrawing::FillRectColor(dc, item.bounds, C(theme.toolbarButton));
            const RECT box{ item.bounds.left + kPad, item.bounds.top + (kRowHeight - kBox) / 2, item.bounds.left + kPad + kBox, item.bounds.top + (kRowHeight + kBox) / 2 };
            EditorDialogStyle::PaintCheckbox(dc, box, theme, item.checked);
            const RECT swatch{ box.right + 8, item.bounds.top + 8, box.right + 16, item.bounds.top + 16 };
            GdiDrawing::FillRectColor(dc, swatch, C(item.swatch));
            EditorDialogStyle::PaintText(dc, RECT{ swatch.right + 8, item.bounds.top, item.bounds.right - 100, item.bounds.bottom }, item.text,
                C(item.checked ? theme.textPrimary : theme.textSecondary), 11);
            EditorDialogStyle::PaintText(dc, RECT{ item.bounds.right - 100, item.bounds.top, item.bounds.right - kPad, item.bounds.bottom }, item.detail,
                C(theme.textDisabled), 10, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case Kind::CounterName:
            EditorDialogStyle::PaintText(dc, RECT{ item.bounds.left + kPad, item.bounds.top, item.bounds.right - 2 * kCellWidth - 4, item.bounds.bottom },
                item.text, C(item.enabled ? theme.textPrimary : theme.textDisabled), 11);
            break;
        case Kind::PanelCounter: case Kind::SceneCounter: {
            if (hot) GdiDrawing::FillRectColor(dc, item.bounds, C(theme.toolbarButton));
            const int centerX = (item.bounds.left + item.bounds.right) / 2;
            const int centerY = (item.bounds.top + item.bounds.bottom) / 2;
            EditorDialogStyle::PaintCheckbox(dc, RECT{ centerX - kBox / 2, centerY - kBox / 2, centerX + kBox / 2, centerY + kBox / 2 }, theme, item.checked, item.enabled);
            break;
        }
        case Kind::ListVisible: case Kind::GraphVisible: case Kind::Line: {
            if (hot) GdiDrawing::FillRectColor(dc, item.bounds, C(theme.toolbarButton));
            const RECT box{ item.bounds.left, item.bounds.top + (kOptionRow - kBox) / 2, item.bounds.left + kBox, item.bounds.top + (kOptionRow + kBox) / 2 };
            EditorDialogStyle::PaintCheckbox(dc, box, theme, item.checked);
            int textLeft = box.right + 8;
            if (item.hasSwatch) {
                GdiDrawing::FillRectColor(dc, RECT{ textLeft, item.bounds.top + 10, textLeft + 8, item.bounds.top + 18 }, C(item.swatch));
                textLeft += 16;
            }
            EditorDialogStyle::PaintText(dc, RECT{ textLeft, item.bounds.top, item.bounds.right, item.bounds.bottom }, item.text, C(theme.textSecondary), 11);
            break;
        }
        case Kind::ListCorner: case Kind::GraphCorner: case Kind::TextSize: case Kind::Refresh: case Kind::Opacity:
            EditorDialogStyle::PaintField(dc, item.bounds, theme, item.detail, hot);
            fieldChevron(item.bounds);
            break;
        case Kind::Restore:
            EditorDialogStyle::PaintButton(dc, item.bounds, theme, item.text, EditorDialogButtonTone::Neutral, hot);
            break;
        case Kind::Done:
            EditorDialogStyle::PaintButton(dc, item.bounds, theme, item.text, EditorDialogButtonTone::Primary, hot);
            break;
        }
        RestoreDC(dc, savedDc);
    }

    // Names of the option rows and of the lines header, which the items above do not carry.
    const auto plainHeader = [&](const RECT& bar, std::string_view title) {
        GdiDrawing::FillRectColor(dc, bar, C(theme.strip));
        EditorDialogStyle::PaintText(dc, RECT{ bar.left + kPad, bar.top, bar.right - kPad, bar.bottom }, title, C(theme.textPrimary), 12, FW_SEMIBOLD);
    };
    plainHeader(layout.appearanceHeader, EditorText("profiler.appearance.title", "Appearance"));
    plainHeader(layout.linesHeader, EditorText("profiler.lines.title", "Live graph lines"));

    // The preview: what the scene shows with this choice, in a stand-in for the scene.
    GdiDrawing::FillRectColor(dc, layout.preview, C(theme.background));
    const int previewWidth = layout.preview.right - layout.preview.left;
    const int previewHeight = layout.preview.bottom - layout.preview.top;
    for (int x = 38; x < previewWidth; x += 39) GdiDrawing::FillRectColor(dc, RECT{ layout.preview.left + x, layout.preview.top + 78, layout.preview.left + x + 1, layout.preview.bottom }, C(theme.gridLine));
    for (int y = 100; y < previewHeight; y += 23) GdiDrawing::FillRectColor(dc, RECT{ layout.preview.left, layout.preview.top + y, layout.preview.right, layout.preview.top + y + 1 }, C(theme.gridLine));
    const ProfilerOverlayModel model = BuildProfilerOverlayModel(sceneContext_->Profiler().Registry(), selection);
    ProfilerOverlayContent content;
    ProfilerOverlayMetrics metrics = ProfilerOverlayMetrics::ForTextSize(selection.overlay.textSize);
    // The preview is a fraction of a scene, so its list is smaller and its graph is a fixed size.
    metrics.scale *= 0.8F;
    metrics.graphWidth = 300.0F;
    metrics.graphHeight = 112.0F;
    AppendProfilerOverlay(content, model, selection.overlay, theme, metrics, static_cast<float>(previewWidth), static_cast<float>(previewHeight));
    static_cast<void>(previewRenderer_.Paint(dc, layout.preview, content.labels, content.shapes));
    for (const RECT& edge : { RECT{ layout.preview.left, layout.preview.top, layout.preview.right, layout.preview.top + 1 },
             RECT{ layout.preview.left, layout.preview.bottom - 1, layout.preview.right, layout.preview.bottom },
             RECT{ layout.preview.left, layout.preview.top, layout.preview.left + 1, layout.preview.bottom },
             RECT{ layout.preview.right - 1, layout.preview.top, layout.preview.right, layout.preview.bottom } }) {
        GdiDrawing::FillRectColor(dc, edge, C(theme.borderPanel));
    }

    EditorDialogStyle::PaintText(dc, RECT{ kPad + 2, layout.footer.top, layout.footer.right - 280, layout.footer.bottom },
        EditorText("profiler.footer.note", "Changes take effect at once, in the profiler and on the scene."), C(theme.textDisabled), 11);

    // The scrollbar of the counters list.
    const int listHeight = layout.countersList.bottom - layout.countersList.top;
    if (layout.countersContent > listHeight) {
        const RECT track{ layout.countersList.right - 8, layout.countersList.top, layout.countersList.right - 2, layout.countersList.bottom };
        const int thumbHeight = std::max(24, listHeight * listHeight / layout.countersContent);
        const int thumbTop = track.top + counterScroll_ * (listHeight - thumbHeight) / (layout.countersContent - listHeight);
        EditorDialogStyle::PaintScrollbar(dc, track, RECT{ track.left, thumbTop, track.right, thumbTop + thumbHeight }, theme);
    }
}

void ProfilerSelectionOverlayWindow::Commit(const ProfilerSelection& selection) {
    EditorConfiguration configuration = sceneContext_->EditorConfig();
    configuration.profiler = selection;
    if (!sceneContext_->SaveEditorConfig(std::move(configuration))) return;
    sceneContext_->Profiler().RefreshOverlay(selection, std::chrono::steady_clock::now(), true);
    // The scene redraws with the new choice, and so does everything else that shows it.
    sceneViewport_->RequestPresent();
    popup_.Invalidate();
    if (popup_.Owner() != nullptr) InvalidateRect(popup_.Owner(), nullptr, FALSE);
}

void ProfilerSelectionOverlayWindow::Activate(const Item& item) {
    using Kind = Item::Kind;
    ProfilerSelection selection = sceneContext_->EditorConfig().profiler;
    const kb::core::ProfilerRegistry& registry = sceneContext_->Profiler().Registry();
    ProfilerOverlayOptions& options = selection.overlay;
    const auto setAll = [&](bool shown) {
        for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
            selection.SetInPanel(registry.Info(counter).id, shown);
            selection.SetOnScene(registry.Info(counter).id, shown);
        }
    };
    switch (item.kind) {
    case Kind::Close: case Kind::Done:
        sceneContext_->Profiler().SetWindowOpen(false);
        Hide();
        if (popup_.Owner() != nullptr) InvalidateRect(popup_.Owner(), nullptr, FALSE);
        return;
    case Kind::Restore: selection = ProfilerSelection{}; break;
    case Kind::Preset: selection.ApplyPreset(static_cast<ProfilerPreset>(item.argument), registry); break;
    case Kind::AreasAll: case Kind::AreasNone:
        for (const ProfilerArea area : kAreas) selection.SetAreaMeasured(area, item.kind == Kind::AreasAll);
        break;
    case Kind::CountersAll: case Kind::CountersNone: setAll(item.kind == Kind::CountersAll); break;
    case Kind::Area: selection.SetAreaMeasured(static_cast<ProfilerArea>(item.argument), !item.checked); break;
    case Kind::PanelCounter: selection.SetInPanel(registry.Info(static_cast<std::size_t>(item.argument)).id, !item.checked); break;
    case Kind::SceneCounter: selection.SetOnScene(registry.Info(static_cast<std::size_t>(item.argument)).id, !item.checked); break;
    case Kind::ListVisible: options.listVisible = !options.listVisible; break;
    case Kind::GraphVisible: options.graphVisible = !options.graphVisible; break;
    case Kind::ListCorner: options.listCorner = Next(options.listCorner, 4); break;
    case Kind::GraphCorner: options.graphCorner = Next(options.graphCorner, 4); break;
    case Kind::TextSize: options.textSize = Next(options.textSize, 3); break;
    case Kind::Refresh: options.refreshPerSecond = NextChoice(ProfilerOverlayOptions::RefreshChoices(), options.refreshPerSecond); break;
    case Kind::Opacity: options.opacityPercent = NextChoice(ProfilerOverlayOptions::OpacityChoices(), options.opacityPercent); break;
    case Kind::Line:
        switch (item.argument) {
        case 0: options.lines.frame = !options.lines.frame; break;
        case 1: options.lines.renderThread = !options.lines.renderThread; break;
        case 2: options.lines.gpu = !options.lines.gpu; break;
        case 3: options.lines.budget = !options.lines.budget; break;
        default: options.lines.alarm = !options.lines.alarm; break;
        }
        break;
    case Kind::GroupHeader: case Kind::CounterName: return;
    }
    Commit(selection);
}

bool ProfilerSelectionOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (sceneContext_ == nullptr) return false;
    switch (message) {
    case WM_MOUSEMOVE: {
        const Layout layout = BuildLayout(popup_.ClientBounds());
        const int index = ItemAt(layout, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (index != hovered_) {
            hovered_ = index;
            popup_.Invalidate();
        }
        TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, popup_.Window(), 0U };
        static_cast<void>(TrackMouseEvent(&track));
        return true;
    }
    case WM_MOUSELEAVE:
        if (hovered_ != -1) {
            hovered_ = -1;
            popup_.Invalidate();
        }
        return true;
    case WM_LBUTTONDOWN: {
        const Layout layout = BuildLayout(popup_.ClientBounds());
        const int index = ItemAt(layout, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (index >= 0) {
            const Item item = layout.items[static_cast<std::size_t>(index)];
            Activate(item);
        }
        return true;
    }
    case WM_RBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONUP:
        return true;
    case WM_MOUSEWHEEL: {
        POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(popup_.Window(), &point);
        const Layout layout = BuildLayout(popup_.ClientBounds());
        if (!Contains(layout.countersColumn, point.x, point.y)) return true;
        const int listHeight = layout.countersList.bottom - layout.countersList.top;
        const int most = std::max(0, layout.countersContent - listHeight);
        counterScroll_ = std::clamp(counterScroll_ - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3 * kRowHeight, 0, most);
        popup_.Invalidate();
        return true;
    }
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
