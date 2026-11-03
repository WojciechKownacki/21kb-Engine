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
#include <optional>
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
// The widest the first two columns get; a narrower window gives the third what it needs first.
constexpr int kAreasWidth = 300;
constexpr int kCountersWidth = 400;
constexpr int kSceneMinWidth = 340;
// The smallest window that holds everything: the body without the preview needs this much.
constexpr int kMinWidth = 900;
constexpr int kMinHeight = 500;
constexpr int kCellWidth = 54;
constexpr int kBox = 14;
constexpr int kPreviewHeight = 240;
constexpr int kMinPreviewHeight = 200;
constexpr int kOptionRow = 28;
constexpr int kFieldWidth = 190;
constexpr int kSearchHeight = 24;
constexpr int kPad = 12;

constexpr std::array<ProfilerArea, 5> kAreas{
    ProfilerArea::Cpu, ProfilerArea::Gpu, ProfilerArea::Render, ProfilerArea::Ecs, ProfilerArea::Memory };

[[nodiscard]] COLORREF C(EditorColor color) noexcept { return EditorDialogStyle::Color(color); }

// A label that sets itself smaller before it is cut and goes on a second line before it is cut at 10 points.
void WrappedLabel(HDC dc, const RECT& room, std::string_view text, COLORREF color, int pointSize = 11, int weight = FW_NORMAL) {
    const int width = static_cast<int>(room.right - room.left);
    const int size = EditorDialogStyle::FittingSize(dc, text, width, pointSize, weight);
    const bool oneLine = EditorDialogStyle::TextWidth(dc, text, size, weight) <= width;
    EditorDialogStyle::PaintText(dc, room, text, color, size, weight,
        oneLine ? DT_LEFT | DT_VCENTER | DT_SINGLELINE : DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT Inset(RECT rect, int dx, int dy) noexcept {
    return RECT{ rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy };
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
        GroupHeader, CounterName, Search, Choice,
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
    [[nodiscard]] bool IsField() const noexcept {
        return kind == Kind::ListCorner || kind == Kind::GraphCorner || kind == Kind::TextSize || kind == Kind::Refresh || kind == Kind::Opacity;
    }
};

struct ProfilerSelectionOverlayWindow::Layout {
    RECT title{}, strip{}, footer{};
    RECT areasColumn{}, countersColumn{}, sceneColumn{};
    RECT areasHint{}, countersSearch{}, countersCaption{}, areasList{}, countersList{};
    RECT preview{}, appearanceHeader{}, linesHeader{};
    RECT panelCaption{}, sceneCaption{};
    // The frame of the open list of choices, empty when none is open.
    RECT choiceFrame{};
    // Where the "all / none" links of the first two headers begin and the dot between them sits; the
    // title of a header ends before them. The links are as wide as their words are in the language.
    RECT footerNote{};
    RECT presetLabel{};
    std::array<int, 2> linksLeft{};
    std::array<RECT, 2> linksDot{};
    int countersContent = 0;
    // Where the counters list is scrolled to: what the wheel set, held inside the list at this size.
    int countersScroll = 0;
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
    if (sceneContext_ != nullptr) sceneContext_->Profiler().Window() = {};
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
    return Press(layout, ItemAt(layout, x, y));
}

bool ProfilerSelectionOverlayWindow::Press(const Layout& layout, int index) {
    using Kind = Item::Kind;
    const Item* item = index >= 0 ? &layout.items[static_cast<std::size_t>(index)] : nullptr;
    sceneContext_->Profiler().CounterSearch().Focus(item != nullptr && item->kind == Kind::Search);
    if (sceneContext_->Profiler().Window().openField >= 0 && (item == nullptr || item->kind != Kind::Choice)) {
        // Like any drop-down, a press anywhere else only closes the list.
        sceneContext_->Profiler().Window().openField = -1;
        popup_.Invalidate();
        return true;
    }
    popup_.Invalidate();
    if (item == nullptr) return false;
    Activate(*item);
    return true;
}

RECT ProfilerSelectionOverlayWindow::ResolveScreenBounds() const noexcept {
    const HWND owner = popup_.Owner();
    if (owner == nullptr || IsWindow(owner) == 0) return {};
    RECT client{};
    GetClientRect(owner, &client);
    const int width = std::min<int>(1180, client.right - client.left - 40);
    const int height = std::min<int>(740, client.bottom - client.top - 40);
    if (width < kMinWidth || height < kMinHeight) return {};
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
    // The third column keeps its room; the other two share the rest, areas a little less than counters.
    const int sharedWidth = std::max(0, width - kSceneMinWidth);
    const int areasWidth = std::min(kAreasWidth, sharedWidth * 3 / 7);
    const int countersWidth = std::min(kCountersWidth, sharedWidth - areasWidth);
    layout.areasColumn = RECT{ 0, bodyTop, areasWidth, bodyBottom };
    layout.countersColumn = RECT{ areasWidth, bodyTop, areasWidth + countersWidth, bodyBottom };
    layout.sceneColumn = RECT{ areasWidth + countersWidth, bodyTop, width, bodyBottom };

    const auto add = [&layout](Item item) { layout.items.push_back(std::move(item)); };
    add({ .kind = Kind::Close, .bounds = RECT{ width - 34, 0, width, kTitleHeight } });

    // Words are measured on the screen's DC, in the language the window shows.
    const HDC measure = GetDC(nullptr);

    // Presets: the one the choice equals is lit.
    const std::array<std::pair<ProfilerPreset, std::string_view>, 4> presets{{
        { ProfilerPreset::Minimal, EditorText("profiler.preset.minimal", "Minimal") },
        { ProfilerPreset::Rendering, EditorText("profiler.preset.rendering", "Rendering") },
        { ProfilerPreset::Memory, EditorText("profiler.preset.memory", "Memory") },
        { ProfilerPreset::Everything, EditorText("profiler.preset.everything", "Everything") },
    }};
    // The preset pills start after the label of the strip, whatever the language makes of it.
    const std::string presetLabel{ EditorText("profiler.preset.label", "Preset") };
    const int pillStart = std::max(96, kPad + 2 + EditorDialogStyle::TextWidth(measure, presetLabel, 11) + 16);
    layout.presetLabel = RECT{ kPad + 2, kTitleHeight, pillStart - 6, bodyTop };
    int pillLeft = pillStart;
    for (const auto& [preset, name] : presets) {
        add({ .kind = Kind::Preset, .argument = static_cast<int>(preset),
              .bounds = RECT{ pillLeft, kTitleHeight + 13, pillLeft + 112, kTitleHeight + 39 },
              .text = std::string{ name }, .checked = MatchesPreset(selection, preset, registry) });
        pillLeft += 120;
    }

    // Column headers carry the "all / none" links.
    const auto links = [&](std::size_t header, const RECT& column, Kind all, Kind none) {
        const int top = column.top + 6;
        const std::string noneText{ EditorText("profiler.select.none", "none") };
        const std::string allText{ EditorText("profiler.select.all", "all") };
        const int noneWidth = EditorDialogStyle::TextWidth(measure, noneText, 10) + 4;
        const int allWidth = EditorDialogStyle::TextWidth(measure, allText, 10) + 4;
        const int noneLeft = column.right - kPad - noneWidth;
        add({ .kind = none, .bounds = RECT{ noneLeft, top, column.right - kPad, top + 18 }, .text = noneText });
        add({ .kind = all, .bounds = RECT{ noneLeft - 14 - allWidth, top, noneLeft - 14, top + 18 }, .text = allText });
        layout.linksLeft[header] = noneLeft - 14 - allWidth;
        layout.linksDot[header] = RECT{ noneLeft - 14, column.top, noneLeft, column.top + kHeaderHeight };
    };
    links(0U, layout.areasColumn, Kind::AreasAll, Kind::AreasNone);
    links(1U, layout.countersColumn, Kind::CountersAll, Kind::CountersNone);

    // Areas, grouped like the engine's own areas of work.
    layout.areasHint = RECT{ 0, bodyTop + kHeaderHeight, areasWidth, bodyTop + kHeaderHeight + 52 };
    layout.areasList = RECT{ 0, layout.areasHint.bottom, areasWidth, bodyBottom };
    struct AreaGroup { std::string_view name; std::array<ProfilerArea, 2> areas; int count; };
    const std::array<AreaGroup, 3> groups{{
        { EditorText("profiler.areagroup.threads", "Threads"), { ProfilerArea::Cpu, ProfilerArea::Gpu }, 2 },
        { EditorText("profiler.areagroup.systems", "Systems"), { ProfilerArea::Render, ProfilerArea::Ecs }, 2 },
        { EditorText("profiler.areagroup.resources", "Resources"), { ProfilerArea::Memory, ProfilerArea::Memory }, 1 },
    }};
    int y = layout.areasList.top;
    for (const AreaGroup& group : groups) {
        add({ .kind = Kind::GroupHeader, .bounds = RECT{ 0, y, areasWidth, y + kRowHeight }, .text = std::string{ group.name } });
        y += kRowHeight;
        for (int index = 0; index < group.count; ++index) {
            const ProfilerArea area = group.areas[static_cast<std::size_t>(index)];
            add({ .kind = Kind::Area, .argument = static_cast<int>(area), .bounds = RECT{ 0, y, areasWidth, y + kRowHeight },
                  .text = std::string{ ProfilerText::Area(area) },
                  .detail = std::string{ area == ProfilerArea::Ecs ? EditorText("profiler.cost.medium", "medium cost") : EditorText("profiler.cost.low", "low cost") },
                  .swatch = profiler_palette::Area(area), .hasSwatch = true, .checked = selection.AreaMeasured(area) });
            y += kRowHeight;
        }
    }

    // Counters: a search box, then the two checkbox columns, which are the panel's list and the scene's list.
    layout.countersSearch = RECT{ layout.countersColumn.left + kPad, bodyTop + kHeaderHeight + 6, layout.countersColumn.right - kPad, bodyTop + kHeaderHeight + 6 + kSearchHeight };
    add({ .kind = Kind::Search, .bounds = layout.countersSearch });
    const int captionTop = layout.countersSearch.bottom + 6;
    layout.countersCaption = RECT{ layout.countersColumn.left, captionTop, layout.countersColumn.right, captionTop + 22 };
    layout.countersList = RECT{ layout.countersColumn.left, layout.countersCaption.bottom, layout.countersColumn.right, bodyBottom };
    layout.sceneCaption = RECT{ layout.countersColumn.right - kCellWidth, layout.countersCaption.top, layout.countersColumn.right, layout.countersCaption.bottom };
    layout.panelCaption = RECT{ layout.sceneCaption.left - kCellWidth, layout.countersCaption.top, layout.sceneCaption.left, layout.countersCaption.bottom };
    // The counters that answer the search, in the order of the registry; their groups show as headers.
    std::vector<std::size_t> shown;
    for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
        if (ProfilerText::CounterMatches(registry.Info(counter), sceneContext_->Profiler().CounterSearch().Query())) shown.push_back(counter);
    }
    std::string_view lastGroup;
    int total = 0;
    for (const std::size_t counter : shown) {
        total += (registry.Info(counter).group != lastGroup ? 2 : 1) * kRowHeight;
        lastGroup = registry.Info(counter).group;
    }
    // A window made taller since the wheel was used holds more of the list, so the scroll is held inside it.
    layout.countersScroll = std::clamp(sceneContext_->Profiler().Window().counterScroll, 0, std::max(0, total - static_cast<int>(layout.countersList.bottom - layout.countersList.top)));
    lastGroup = {};
    int content = 0;
    for (const std::size_t counter : shown) {
        const kb::core::ProfilerCounterInfo& info = registry.Info(counter);
        if (info.group != lastGroup) {
            lastGroup = info.group;
            const int top = layout.countersList.top + content - layout.countersScroll;
            add({ .kind = Kind::GroupHeader, .bounds = RECT{ layout.countersList.left, top, layout.countersList.right, top + kRowHeight },
                  .text = std::string{ ProfilerText::Group(info.group) } });
            content += kRowHeight;
        }
        const int top = layout.countersList.top + content - layout.countersScroll;
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
    // The graph's lines sit in two columns, or in one when the column is narrow; the preview gets
    // the height that is left under the rest, and none at all when that is too little to be a preview.
    const int lineColumns = sceneRight - sceneLeft >= 2 * 120 ? 2 : 1;
    const int lineRows = (5 + lineColumns - 1) / lineColumns;
    const int restHeight = 10 + kHeaderHeight + 10 + 5 * kOptionRow + 10 + kHeaderHeight + 10 + lineRows * kOptionRow + 10;
    const int roomForPreview = (bodyBottom - bodyTop) - kHeaderHeight - 10 - restHeight;
    const int previewHeight = roomForPreview < kMinPreviewHeight ? 0 : std::min(kPreviewHeight, roomForPreview);
    layout.preview = RECT{ sceneLeft, bodyTop + kHeaderHeight + 10, sceneRight, bodyTop + kHeaderHeight + 10 + previewHeight };
    layout.appearanceHeader = RECT{ layout.sceneColumn.left, layout.preview.bottom + 10, layout.sceneColumn.right, layout.preview.bottom + 10 + kHeaderHeight };
    int rowTop = layout.appearanceHeader.bottom + 10;
    const auto option = [&](Kind check, Kind field, std::string_view label, std::string value, bool checkable, bool checked) {
        const RECT row{ sceneLeft, rowTop, sceneRight, rowTop + kOptionRow };
        const RECT fieldRect{ sceneRight - std::min(kFieldWidth, (sceneRight - sceneLeft) * 3 / 5), rowTop + 2, sceneRight, rowTop + kOptionRow - 2 };
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
    const int columnWidth = (sceneRight - sceneLeft) / lineColumns;
    for (const LineChoice& line : lines) {
        const int left = sceneLeft + (line.id % lineColumns) * columnWidth;
        const int top = layout.linesHeader.bottom + 10 + (line.id / lineColumns) * kOptionRow;
        add({ .kind = Kind::Line, .argument = line.id, .bounds = RECT{ left, top, left + columnWidth - 8, top + kOptionRow },
              .text = std::string{ line.label }, .swatch = line.id == 0 ? theme_.textPrimary : line.color, .hasSwatch = true, .checked = line.checked });
    }

    // The buttons are as wide as their words, and the note of the footer has what is left of the line.
    const std::string restoreText{ EditorText("profiler.button.restore", "Restore defaults") };
    const std::string doneText{ EditorText("profiler.button.done", "Done") };
    const int doneWidth = std::max(90, EditorDialogStyle::TextWidth(measure, doneText, 12, FW_SEMIBOLD) + 32);
    const int restoreWidth = std::max(160, EditorDialogStyle::TextWidth(measure, restoreText, 12, FW_SEMIBOLD) + 32);
    add({ .kind = Kind::Restore, .bounds = RECT{ width - kPad - doneWidth - 8 - restoreWidth, bodyBottom + 10, width - kPad - doneWidth - 8, bodyBottom + 36 },
          .text = restoreText });
    add({ .kind = Kind::Done, .bounds = RECT{ width - kPad - doneWidth, bodyBottom + 10, width - kPad, bodyBottom + 36 }, .text = doneText });
    layout.footerNote = RECT{ kPad + 2, bodyBottom, width - kPad - doneWidth - 8 - restoreWidth - 12, client.bottom };
    ReleaseDC(nullptr, measure);

    // The list of choices of the open field comes last, so it is over everything and is hit first.
    const auto open = std::ranges::find_if(layout.items, [this](const Item& item) { return static_cast<int>(item.kind) == sceneContext_->Profiler().Window().openField; });
    if (open != layout.items.end()) {
        const Kind field = open->kind;
        const RECT fieldBounds = open->bounds;
        struct Choice { int value; std::string text; bool current; };
        std::vector<Choice> choices;
        switch (field) {
        case Kind::ListCorner: case Kind::GraphCorner: {
            const ProfilerCorner current = field == Kind::ListCorner ? options.listCorner : options.graphCorner;
            for (int corner = 0; corner < 4; ++corner) choices.push_back({ corner, CornerText(static_cast<ProfilerCorner>(corner)), corner == static_cast<int>(current) });
            break;
        }
        case Kind::TextSize:
            for (int size = 0; size < 3; ++size) choices.push_back({ size, TextSizeText(static_cast<ProfilerTextSize>(size)), size == static_cast<int>(options.textSize) });
            break;
        case Kind::Refresh:
            for (const std::uint8_t rate : ProfilerOverlayOptions::RefreshChoices()) choices.push_back({ rate, RefreshText(rate), rate == options.refreshPerSecond });
            break;
        default:
            for (const std::uint8_t percent : ProfilerOverlayOptions::OpacityChoices()) choices.push_back({ percent, std::format("{}%", percent), percent == options.opacityPercent });
            break;
        }
        const int listHeight = static_cast<int>(choices.size()) * kRowHeight + 4;
        int top = fieldBounds.bottom + 2;
        // Near the bottom of the window the list opens upwards.
        if (top + listHeight > client.bottom - 4) top = fieldBounds.top - 2 - listHeight;
        layout.choiceFrame = RECT{ fieldBounds.left, top, fieldBounds.right, top + listHeight };
        int choiceTop = top + 2;
        for (const Choice& choice : choices) {
            add({ .kind = Kind::Choice, .argument = choice.value, .bounds = RECT{ fieldBounds.left + 1, choiceTop, fieldBounds.right - 1, choiceTop + kRowHeight },
                  .text = choice.text, .checked = choice.current });
            choiceTop += kRowHeight;
        }
    }
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
    EditorDialogStyle::PaintText(dc, layout.presetLabel, EditorText("profiler.preset.label", "Preset"), C(theme.textSecondary), 11);

    // Column bodies and headers.
    const auto header = [&](const RECT& column, int number, std::string_view title, std::optional<std::size_t> links) {
        const RECT bar{ column.left, column.top, column.right, column.top + kHeaderHeight };
        GdiDrawing::FillRectColor(dc, bar, C(theme.strip));
        const RECT badge{ column.left + kPad, bar.top + 6, column.left + kPad + 18, bar.top + 24 };
        EditorDialogStyle::PaintBadge(dc, badge, theme, std::to_string(number));
        // The title gets the room the links leave; a narrow window sets it smaller.
        const RECT titleRect{ badge.right + 8, bar.top, links ? layout.linksLeft[*links] - 8 : bar.right - kPad, bar.bottom };
        WrappedLabel(dc, titleRect, title, C(theme.textPrimary), 12, FW_SEMIBOLD);
        // The dot between the two links of the header, which are items of their own.
        if (links) {
            EditorDialogStyle::PaintText(dc, layout.linksDot[*links], "\xC2\xB7",
                C(theme.textDisabled), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    GdiDrawing::FillRectColor(dc, RECT{ layout.areasColumn.left, layout.areasColumn.top + kHeaderHeight, layout.areasColumn.right, layout.areasColumn.bottom }, C(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{ layout.countersColumn.left, layout.countersColumn.top + kHeaderHeight, layout.countersColumn.right, layout.countersColumn.bottom }, C(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{ layout.sceneColumn.left, layout.sceneColumn.top + kHeaderHeight, layout.sceneColumn.right, layout.sceneColumn.bottom }, C(theme.panel));
    header(layout.areasColumn, 1, EditorText("profiler.areas.title", "Areas - what to measure"), 0U);
    header(layout.countersColumn, 2, EditorText("profiler.counters.title", "Counters - what to show"), 1U);
    header(layout.sceneColumn, 3, EditorText("profiler.preview.title", "Scene preview"), std::nullopt);
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
            // The cost takes the room its words need in the language; the name has what is left.
            const int detailLeft = item.bounds.right - kPad - std::max(88, EditorDialogStyle::TextWidth(dc, item.detail, 10) + 4);
            EditorDialogStyle::PaintText(dc, RECT{ swatch.right + 8, item.bounds.top, detailLeft - 4, item.bounds.bottom }, item.text,
                C(item.checked ? theme.textPrimary : theme.textSecondary), 11);
            EditorDialogStyle::PaintText(dc, RECT{ detailLeft, item.bounds.top, item.bounds.right - kPad, item.bounds.bottom }, item.detail,
                C(theme.textDisabled), 10, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case Kind::CounterName:
            // In the counters list the room ends before the two columns of boxes; the labels of the options have it all.
            WrappedLabel(dc, RECT{ item.bounds.left + kPad, item.bounds.top, item.bounds.right - (inCounters ? 2 * kCellWidth + 4 : 0), item.bounds.bottom },
                item.text, C(item.enabled ? theme.textPrimary : theme.textDisabled));
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
            WrappedLabel(dc, RECT{ textLeft, item.bounds.top, item.bounds.right, item.bounds.bottom }, item.text, C(theme.textSecondary));
            break;
        }
        case Kind::ListCorner: case Kind::GraphCorner: case Kind::TextSize: case Kind::Refresh: case Kind::Opacity:
            EditorDialogStyle::PaintField(dc, item.bounds, theme, item.detail, hot || static_cast<int>(item.kind) == sceneContext_->Profiler().Window().openField);
            fieldChevron(item.bounds);
            break;
        case Kind::Search: {
            const EditorHierarchySearchState& search = sceneContext_->Profiler().CounterSearch();
            const bool focused = search.IsFocused();
            GdiDrawing::DrawSharpFrame(dc, item.bounds, C(theme.chrome), C(focused ? theme.accent : theme.borderPanel));
            const int iconTop = item.bounds.top + (item.bounds.bottom - item.bounds.top - 12) / 2;
            HeroIconPainter::Draw(dc, RECT{ item.bounds.left + 7, iconTop, item.bounds.left + 19, iconTop + 12 }, HeroIconKind::MagnifyingGlass, C(theme.textDisabled), 1);
            const RECT textRect{ item.bounds.left + 25, item.bounds.top, item.bounds.right - 6, item.bounds.bottom };
            if (search.Query().empty() && !focused) {
                EditorDialogStyle::PaintText(dc, textRect, EditorLocalization::Format("profiler.search.placeholder", "Search counters... ({count} available)",
                    { { "count", std::to_string(sceneContext_->Profiler().Registry().CounterCount()) } }), C(theme.textDisabled), 11);
            } else {
                // The caret is the field's only sign of focus besides its border.
                EditorDialogStyle::PaintText(dc, textRect, std::string{ search.Query() } + (focused ? "|" : ""), C(theme.textPrimary), 11);
            }
            break;
        }
        case Kind::Choice: break;  // painted last, over the rest
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
    // A window too short for it has no preview.
    if (layout.preview.bottom > layout.preview.top) {
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
    }

    WrappedLabel(dc, layout.footerNote, EditorText("profiler.footer.note", "Changes take effect at once, in the profiler and on the scene."), C(theme.textDisabled));

    if (layout.countersContent == 0) {
        EditorDialogStyle::PaintText(dc, layout.countersList, EditorText("profiler.search.none", "No counter answers that search."), C(theme.textDisabled), 11,
            FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // The scrollbar of the counters list.
    const int listHeight = layout.countersList.bottom - layout.countersList.top;
    if (layout.countersContent > listHeight) {
        const RECT track{ layout.countersList.right - 8, layout.countersList.top, layout.countersList.right - 2, layout.countersList.bottom };
        const int thumbHeight = std::max(24, listHeight * listHeight / layout.countersContent);
        const int thumbTop = track.top + layout.countersScroll * (listHeight - thumbHeight) / (layout.countersContent - listHeight);
        EditorDialogStyle::PaintScrollbar(dc, track, RECT{ track.left, thumbTop, track.right, thumbTop + thumbHeight }, theme);
    }

    // The open list of choices, over everything: a framed list with the current choice marked.
    if (layout.choiceFrame.right > layout.choiceFrame.left) {
        GdiDrawing::DrawSharpFrame(dc, layout.choiceFrame, C(theme.panel), C(theme.accent));
        for (std::size_t index = 0U; index < layout.items.size(); ++index) {
            const Item& item = layout.items[index];
            if (item.kind != Kind::Choice) continue;
            if (static_cast<int>(index) == hovered_) GdiDrawing::FillRectColor(dc, item.bounds, C(theme.toolbarButton));
            if (item.checked) GdiDrawing::FillRectColor(dc, RECT{ item.bounds.left, item.bounds.top, item.bounds.left + 3, item.bounds.bottom }, C(theme.accent));
            EditorDialogStyle::PaintText(dc, RECT{ item.bounds.left + 10, item.bounds.top, item.bounds.right - 6, item.bounds.bottom }, item.text,
                C(item.checked ? theme.textPrimary : theme.textSecondary), 11, item.checked ? FW_SEMIBOLD : FW_NORMAL);
        }
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
    // "all" and "none" act on the counters the search lists.
    const auto setAll = [&](bool shown) {
        for (std::size_t counter = 0U; counter < registry.CounterCount(); ++counter) {
            if (!ProfilerText::CounterMatches(registry.Info(counter), sceneContext_->Profiler().CounterSearch().Query())) continue;
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
    case Kind::ListCorner: case Kind::GraphCorner: case Kind::TextSize: case Kind::Refresh: case Kind::Opacity:
        sceneContext_->Profiler().Window().openField = static_cast<int>(item.kind);
        popup_.Invalidate();
        return;
    case Kind::Choice:
        switch (static_cast<Kind>(sceneContext_->Profiler().Window().openField)) {
        case Kind::ListCorner: options.listCorner = static_cast<ProfilerCorner>(item.argument); break;
        case Kind::GraphCorner: options.graphCorner = static_cast<ProfilerCorner>(item.argument); break;
        case Kind::TextSize: options.textSize = static_cast<ProfilerTextSize>(item.argument); break;
        case Kind::Refresh: options.refreshPerSecond = static_cast<std::uint8_t>(item.argument); break;
        default: options.opacityPercent = static_cast<std::uint8_t>(item.argument); break;
        }
        sceneContext_->Profiler().Window().openField = -1;
        break;
    case Kind::Search: return;
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
        static_cast<void>(Press(layout, ItemAt(layout, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))));
        return true;
    }
    case WM_RBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONUP:
        return true;
    case WM_MOUSEWHEEL: {
        POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(popup_.Window(), &point);
        const Layout layout = BuildLayout(popup_.ClientBounds());
        if (!Contains(layout.countersColumn, point.x, point.y)) return true;
        sceneContext_->Profiler().Window().openField = -1;
        const int listHeight = layout.countersList.bottom - layout.countersList.top;
        const int most = std::max(0, layout.countersContent - listHeight);
        sceneContext_->Profiler().Window().counterScroll = std::clamp(layout.countersScroll - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3 * kRowHeight, 0, most);
        popup_.Invalidate();
        return true;
    }
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
