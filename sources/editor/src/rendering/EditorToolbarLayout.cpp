#include "rendering/EditorToolbarLayout.hpp"

#include "localization/EditorLocalization.hpp"
#include "rendering/EditorMenuTable.hpp"
#include "rendering/gdi/EditorFontCache.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr int kBarControlHeight = 36;
constexpr int kBarEdgeInset = 8;
constexpr int kBarGap = 2;
constexpr int kProjectButtonWidth = 116;
constexpr int kSceneButtonWidth = 156;
constexpr int kBranchLabelWidth = 64;
constexpr int kMenuAfterBranchGap = 12;
constexpr int kPlayButtonWidth = 92;
constexpr int kPlayOptionsWidth = 24;
constexpr int kTransportButtonSize = 38;
constexpr int kTransportButtonGap = 4;
constexpr int kBuildButtonWidth = 100;
constexpr int kPublishButtonWidth = 92;
constexpr int kWindowControlWidth = 46;
constexpr int kMenuItemHeight = kBarControlHeight;
// Space kept between the last menu title and the transport controls.
constexpr int kMenuToTransportGap = 24;
// Where the menu titles start: past the project, scene and branch group.
constexpr int kMenuLeftInset = kBarEdgeInset + kProjectButtonWidth + kBarGap + kSceneButtonWidth + kBarGap + kBranchLabelWidth + kMenuAfterBranchGap;
constexpr int kDropdownWidth = 260;
constexpr int kProjectDropdownWidth = 300;
constexpr int kRecentProjectsDropdownWidth = 400;
constexpr int kPublishDropdownWidth = 400;
constexpr int kDropdownRowHeight = 46;
constexpr int kDropdownInset = 4;

constexpr std::array<EditorMenuDescriptor, 6> kMenus{{
    { EditorMenuCommand::File, 44 },
    { EditorMenuCommand::Edit, 44 },
    { EditorMenuCommand::Layout, 44 },
    { EditorMenuCommand::Options, 44 },
    { EditorMenuCommand::World, 44 },
    { EditorMenuCommand::Help, 44 },
}};

// The Layout menu is the only one whose length the project decides, so the row
// geometry has to have room for the longest one the menu model can produce.
static_assert(EditorMenuRects::MaximumRows >= EditorLayoutMenuModel::MaximumRows,
    "the menu geometry must fit every row the Layout menu can list");

constexpr int kMenuTitlePadding = 8;
// Room beside a dropdown row's text: the insets, the icon column and the right padding.
constexpr int kDropdownTextAllowance = 62;
// Room a row's shortcut takes on its right edge, beyond the gap to its label.
constexpr int kDropdownShortcutGap = 24;

// Width of `text` in the editor's font, measured the way GDI will draw it.
[[nodiscard]] int TextWidth(std::string_view text, int pointSize) noexcept {
    try {
        static HDC measuring = CreateCompatibleDC(nullptr);
        if (measuring == nullptr || text.empty()) {
            return 0;
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        const HGDIOBJ previous = SelectObject(measuring, EditorFontCache::Acquire(pointSize, FW_NORMAL));
        SIZE size{};
        const BOOL measured = GetTextExtentPoint32W(measuring, wide.data(), length, &size);
        SelectObject(measuring, previous);
        return measured != 0 ? static_cast<int>(size.cx) : 0;
    } catch (...) {
        return 0;
    }
}

[[nodiscard]] int MenuTitleWidth(const EditorMenuDescriptor& descriptor) noexcept {
    try {
        return std::max(descriptor.width,
            TextWidth(EditorToolbarLayout::MenuLabel(descriptor.command), EditorToolbarLayout::MenuFontSize) +
                kMenuTitlePadding * 2);
    } catch (...) {
        return descriptor.width;
    }
}

[[nodiscard]] int DropdownWidth(EditorMenuCommand menu) noexcept {
    if (menu == EditorMenuCommand::Project || menu == EditorMenuCommand::Scene || menu == EditorMenuCommand::PlayOptions) {
        return kProjectDropdownWidth;
    }
    if (menu == EditorMenuCommand::Publish) {
        return kPublishDropdownWidth;
    }
    if (menu == EditorMenuCommand::RecentProjects) {
        return kRecentProjectsDropdownWidth;
    }
    int width = kDropdownWidth;
    try {
        for (const EditorMenuRowSpec& row : EditorMenuTable::FixedRows(menu)) {
            const std::string_view shortcut = row.shortcut;
            width = std::max(width, std::max(TextWidth(row.label.Resolve(), EditorToolbarLayout::MenuFontSize),
                    TextWidth(row.detail.Resolve(), EditorToolbarLayout::DropdownDetailFontSize)) +
                kDropdownTextAllowance +
                (shortcut.empty() ? 0 : TextWidth(shortcut, EditorToolbarLayout::MenuFontSize) + kDropdownShortcutGap));
        }
    } catch (...) {
        return kDropdownWidth;
    }
    return width;
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT ToolbarRect(const RECT& toolbar, int left, int width, int height) noexcept {
    const int toolbarHeight = static_cast<int>(toolbar.bottom - toolbar.top);
    const int top = toolbar.top + std::max(0, (toolbarHeight - height) / 2);
    return RECT{
        .left = toolbar.left + left,
        .top = top,
        .right = toolbar.left + left + width,
        .bottom = top + height,
    };
}

} // namespace

const std::array<EditorMenuDescriptor, 6>& EditorToolbarLayout::MenuDescriptors() noexcept {
    return kMenus;
}

EditorMenuRects EditorToolbarLayout::ResolveMenu(
    const RECT& rect, EditorMenuCommand openMenu, int rowCount) noexcept {
    EditorMenuRects menu{};
    menu.menuBar = rect;
    int left = rect.left + kMenuLeftInset;
    for (const EditorMenuDescriptor& descriptor : kMenus) {
        const int width = MenuTitleWidth(descriptor);
        const int top = rect.top + std::max(0, (static_cast<int>(rect.bottom - rect.top) - kMenuItemHeight) / 2);
        RECT item{
            .left = left,
            .top = top,
            .right = left + width,
            .bottom = top + kMenuItemHeight,
        };
        switch (descriptor.command) {
        case EditorMenuCommand::File:
            menu.file = item;
            break;
        case EditorMenuCommand::Edit:
            menu.edit = item;
            break;
        case EditorMenuCommand::Layout:
            menu.layout = item;
            break;
        case EditorMenuCommand::Options:
            menu.options = item;
            break;
        case EditorMenuCommand::World:
            menu.world = item;
            break;
        case EditorMenuCommand::Help:
            menu.help = item;
            break;
        case EditorMenuCommand::None:
        default:
            break;
        }
        left += width;
    }

    menu.project = ToolbarRect(rect, kBarEdgeInset, kProjectButtonWidth, kBarControlHeight);
    menu.scene = ToolbarRect(rect, kBarEdgeInset + kProjectButtonWidth + kBarGap, kSceneButtonWidth, kBarControlHeight);
    const EditorToolbarRects toolbar = ResolveToolbar(rect);
    menu.playOptions = toolbar.playOptionsButton;
    menu.publish = toolbar.publishButton;
    const RECT anchor = MenuRectByCommand(menu, openMenu);
    if (openMenu != EditorMenuCommand::None) {
        menu.dropdownRowCount = std::clamp(
            rowCount, 0, static_cast<int>(EditorMenuRects::MaximumRows));
        const int dropdownWidth = DropdownWidth(openMenu);
        // Publish sits at the right of the bar, so its dropdown lines up with the button's right edge.
        const int dropX = openMenu == EditorMenuCommand::Publish
            ? static_cast<int>(anchor.right) - dropdownWidth
            : std::min(std::max(rect.left + 4, anchor.left), std::max(rect.left + 4, rect.right - dropdownWidth - 4));
        // Hung from the button's bottom edge, so the two read as one surface.
        const int dropY = static_cast<int>(anchor.bottom);
        int rowTop = dropY + kDropdownInset;
        for (int i = 0; i < menu.dropdownRowCount; ++i) {
            if (EditorMenuTable::StartsGroup(openMenu, i, menu.dropdownRowCount)) {
                rowTop += DropdownSeparatorHeight;
            }
            if (!EditorMenuTable::Heading(openMenu, i, menu.dropdownRowCount).empty()) {
                rowTop += DropdownHeadingHeight;
            }
            menu.dropdownRows[static_cast<std::size_t>(i)] = RECT{
                dropX + kDropdownInset,
                rowTop,
                dropX + dropdownWidth - kDropdownInset,
                rowTop + kDropdownRowHeight,
            };
            rowTop += kDropdownRowHeight;
        }
        menu.dropdown = RECT{ dropX, dropY, dropX + dropdownWidth, rowTop + kDropdownInset };
    }
    return menu;
}

EditorStatusBarRects EditorToolbarLayout::ResolveStatusBar(const RECT& rect) noexcept {
    constexpr int inset = 10;
    constexpr int gap = 16;
    // Room for the label beside each icon in the editor's font, so a longer translation or a
    // four-digit count still fits.
    const auto widthOf = [](std::string_view text) noexcept {
        return 24 + TextWidth(text, EditorFontCache::UiPointSize);
    };
    EditorStatusBarRects bar{};
    bar.bar = rect;
    int left = static_cast<int>(rect.left) + inset;
    const auto next = [&](int width) noexcept {
        const RECT item{ left, rect.top, std::min(static_cast<int>(rect.right), left + width), rect.bottom };
        left = item.right + gap;
        return item;
    };
    try {
        bar.state = next(std::max(widthOf(EditorText("statusBar.ready", "Ready")), widthOf(EditorText("toolbar.playing", "Playing"))));
        bar.errors = next(widthOf(EditorLocalization::Format("statusBar.errors", "{count} errors", { { "count", "9999" } })));
        bar.warnings = next(widthOf(EditorLocalization::Format("statusBar.warnings", "{count} warnings", { { "count", "9999" } })));
        bar.askAgent = next(widthOf(EditorText("statusBar.askAgent", "Ask Agent")));
    } catch (...) {
        return bar;
    }
    bar.backend = RECT{ std::min(static_cast<int>(rect.right), left), rect.top, static_cast<int>(rect.right) - inset, rect.bottom };
    return bar;
}

int EditorToolbarLayout::MenuTitlesRight(const RECT& rect) noexcept {
    int right = static_cast<int>(rect.left) + kMenuLeftInset;
    for (const EditorMenuDescriptor& descriptor : kMenus) {
        right += MenuTitleWidth(descriptor);
    }
    return right;
}

// One row holds everything: the project group, the menus, the transport in the middle of
// the window (or just past the menus when the window is too narrow), then Build, Publish
// and the window's own controls. Save lives in the File menu and on Ctrl+S.
EditorToolbarRects EditorToolbarLayout::ResolveToolbar(const RECT& rect) noexcept {
    EditorToolbarRects toolbar{};
    toolbar.toolbar = rect;
    int leftGroup = kBarEdgeInset;
    toolbar.projectButton = ToolbarRect(rect, leftGroup, kProjectButtonWidth, kBarControlHeight);
    leftGroup += kProjectButtonWidth + kBarGap;
    toolbar.sceneButton = ToolbarRect(rect, leftGroup, kSceneButtonWidth, kBarControlHeight);
    leftGroup += kSceneButtonWidth + kBarGap;
    toolbar.branchLabel = ToolbarRect(rect, leftGroup, kBranchLabelWidth, kBarControlHeight);

    const int totalWidth = kPlayButtonWidth + 1 + kPlayOptionsWidth + kTransportButtonGap + kTransportButtonSize * 2 + kTransportButtonGap;
    const int toolbarWidth = static_cast<int>(rect.right - rect.left);
    int left = std::max(std::max(0, (toolbarWidth - totalWidth) / 2), MenuTitlesRight(rect) + kMenuToTransportGap - static_cast<int>(rect.left));
    toolbar.playButton = ToolbarRect(rect, left, kPlayButtonWidth, kBarControlHeight);
    left += kPlayButtonWidth + 1;
    toolbar.playOptionsButton = ToolbarRect(rect, left, kPlayOptionsWidth, kBarControlHeight);
    left += kPlayOptionsWidth + kTransportButtonGap;
    toolbar.pauseButton = ToolbarRect(rect, left, kTransportButtonSize, kBarControlHeight);
    left += kTransportButtonSize + kTransportButtonGap;
    toolbar.stopButton = ToolbarRect(rect, left, kTransportButtonSize, kBarControlHeight);

    const int windowControlsLeft = toolbarWidth - kWindowControlWidth * 3;
    const auto windowControl = [&](int index) noexcept {
        return RECT{
            .left = rect.left + windowControlsLeft + (kWindowControlWidth * index),
            .top = rect.top,
            .right = rect.left + windowControlsLeft + (kWindowControlWidth * (index + 1)),
            .bottom = rect.bottom,
        };
    };
    toolbar.minimizeButton = windowControl(0);
    toolbar.maximizeButton = windowControl(1);
    toolbar.closeButton = windowControl(2);

    const int publishLeft = windowControlsLeft - kBarEdgeInset - kPublishButtonWidth;
    const int buildLeft = publishLeft - kBarGap - kBuildButtonWidth;
    if (rect.left + buildLeft >= toolbar.stopButton.right + kBarEdgeInset) {
        toolbar.publishButton = ToolbarRect(rect, publishLeft, kPublishButtonWidth, kBarControlHeight);
        toolbar.buildButton = ToolbarRect(rect, buildLeft, kBuildButtonWidth, kBarControlHeight);
    }
    return toolbar;
}

EditorTopBarItem EditorToolbarLayout::HitTestTopBarItem(const EditorToolbarRects& rects, int x, int y) noexcept {
    const std::array<std::pair<RECT, EditorTopBarItem>, 9> items{{
        { rects.projectButton, EditorTopBarItem::Project },
        { rects.sceneButton, EditorTopBarItem::Scene },
        { rects.branchLabel, EditorTopBarItem::Branch },
        { rects.playOptionsButton, EditorTopBarItem::PlayOptions },
        { rects.buildButton, EditorTopBarItem::Build },
        { rects.publishButton, EditorTopBarItem::Publish },
        { rects.minimizeButton, EditorTopBarItem::Minimize },
        { rects.maximizeButton, EditorTopBarItem::Maximize },
        { rects.closeButton, EditorTopBarItem::Close },
    }};
    for (const auto& [rect, item] : items) {
        if (PointInRect(rect, x, y)) {
            return item;
        }
    }
    return EditorTopBarItem::None;
}

bool EditorToolbarLayout::IsInteractive(const EditorMenuRects& menu, const EditorToolbarRects& toolbar, int x, int y) noexcept {
    return HitTestMenu(menu, x, y) != EditorMenuCommand::None
        || HitTestTransport(toolbar, x, y) != EditorTransportCommand::None
        || HitTestTopBarItem(toolbar, x, y) != EditorTopBarItem::None;
}

EditorMenuCommand EditorToolbarLayout::HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept {
    if (PointInRect(rects.file, x, y)) {
        return EditorMenuCommand::File;
    }
    if (PointInRect(rects.edit, x, y)) {
        return EditorMenuCommand::Edit;
    }
    if (PointInRect(rects.layout, x, y)) {
        return EditorMenuCommand::Layout;
    }
    if (PointInRect(rects.options, x, y)) {
        return EditorMenuCommand::Options;
    }
    if (PointInRect(rects.world, x, y)) {
        return EditorMenuCommand::World;
    }
    if (PointInRect(rects.help, x, y)) {
        return EditorMenuCommand::Help;
    }
    if (PointInRect(rects.project, x, y)) {
        return EditorMenuCommand::Project;
    }
    if (PointInRect(rects.scene, x, y)) {
        return EditorMenuCommand::Scene;
    }
    if (PointInRect(rects.playOptions, x, y)) {
        return EditorMenuCommand::PlayOptions;
    }
    if (PointInRect(rects.publish, x, y)) {
        return EditorMenuCommand::Publish;
    }
    return EditorMenuCommand::None;
}

std::optional<int> EditorToolbarLayout::HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept {
    const auto count = static_cast<std::size_t>(std::max(0, rects.dropdownRowCount));
    for (std::size_t i = 0; i < std::min(count, rects.dropdownRows.size()); ++i) {
        if (PointInRect(rects.dropdownRows[i], x, y)) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

EditorTransportCommand EditorToolbarLayout::HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept {
    if (PointInRect(rects.playButton, x, y)) {
        return EditorTransportCommand::Play;
    }
    if (PointInRect(rects.pauseButton, x, y)) {
        return EditorTransportCommand::Pause;
    }
    if (PointInRect(rects.stopButton, x, y)) {
        return EditorTransportCommand::Stop;
    }
    return EditorTransportCommand::None;
}

bool EditorToolbarLayout::HitTestSave(const EditorToolbarRects& rects, int x, int y) noexcept {
    return PointInRect(rects.saveButton, x, y);
}

RECT EditorToolbarLayout::MenuRectByCommand(const EditorMenuRects& rects, EditorMenuCommand menu) noexcept {
    switch (menu) {
    case EditorMenuCommand::File:
        return rects.file;
    case EditorMenuCommand::Edit:
        return rects.edit;
    case EditorMenuCommand::Layout:
        return rects.layout;
    case EditorMenuCommand::Options:
        return rects.options;
    case EditorMenuCommand::World:
        return rects.world;
    case EditorMenuCommand::Help:
        return rects.help;
    case EditorMenuCommand::Project:
    case EditorMenuCommand::RecentProjects:
        return rects.project;
    case EditorMenuCommand::Scene:
        return rects.scene;
    case EditorMenuCommand::PlayOptions:
        return rects.playOptions;
    case EditorMenuCommand::Publish:
        return rects.publish;
    case EditorMenuCommand::None:
    default:
        return RECT{};
    }
}

std::string_view EditorToolbarLayout::MenuLabel(EditorMenuCommand menu) {
    switch (menu) {
    case EditorMenuCommand::File:
        return EditorText("menu.file", "File");
    case EditorMenuCommand::Edit:
        return EditorText("menu.edit", "Edit");
    case EditorMenuCommand::Layout:
        return EditorText("menu.layout", "Layout");
    case EditorMenuCommand::Options:
        return EditorText("menu.options", "Options");
    case EditorMenuCommand::World:
        return EditorText("menu.world", "World");
    case EditorMenuCommand::Help:
        return EditorText("menu.help", "Help");
    case EditorMenuCommand::None:
    default:
        return {};
    }
}

} // namespace kb::editor

#endif
