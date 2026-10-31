#include "rendering/EditorToolbarLayout.hpp"

#include "localization/EditorLocalization.hpp"
#include "rendering/gdi/EditorFontCache.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

constexpr int kTransportButtonSize = 30;
constexpr int kTransportButtonGap = 5;
constexpr int kToolbarSideInset = 12;
constexpr int kSaveButtonWidth = 64;
constexpr int kSaveButtonHeight = 28;
constexpr int kMenuLeftInset = 8;
constexpr int kMenuItemHeight = 28;
// Space kept between the last menu title and the transport controls.
constexpr int kMenuToTransportGap = 24;
constexpr int kDropdownTopGap = 4;
constexpr int kDropdownWidth = 230;
constexpr int kDropdownRowHeight = 30;

constexpr std::array<EditorMenuDescriptor, 6> kMenus{{
    { EditorMenuCommand::File, 54 },
    { EditorMenuCommand::Edit, 54 },
    { EditorMenuCommand::Layout, 72 },
    { EditorMenuCommand::Options, 82 },
    { EditorMenuCommand::World, 68 },
    { EditorMenuCommand::Help, 58 },
}};

// How many of those rows each menu actually shows. Layout counts zero here: its
// length comes from the project, not from the build.
constexpr std::array<int, 6> kDropdownRowCounts{ 5, 3, 0, 6, 11, 4 };

// The Layout menu is the only one whose length the project decides, so the row
// geometry has to have room for the longest one the menu model can produce.
static_assert(EditorMenuRects::MaximumRows >= EditorLayoutMenuModel::MaximumRows,
    "the menu geometry must fit every row the Layout menu can list");

constexpr int kMenuTitlePadding = 12;
// Room beside a dropdown row's text: its icon on the left and breathing space on the right.
constexpr int kDropdownTextAllowance = 76;
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
    int width = kDropdownWidth;
    try {
        for (int row = 0; row < EditorToolbarLayout::FixedRowCount(menu); ++row) {
            const std::string_view shortcut = EditorToolbarLayout::DropdownShortcut(menu, row);
            width = std::max(width, TextWidth(EditorToolbarLayout::DropdownLabel(menu, row), EditorFontCache::UiPointSize) +
                kDropdownTextAllowance +
                (shortcut.empty() ? 0 : TextWidth(shortcut, EditorFontCache::UiPointSize) + kDropdownShortcutGap));
        }
    } catch (...) {
        return kDropdownWidth;
    }
    return width;
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT ButtonRect(const RECT& toolbar, int left, int size) noexcept {
    const int toolbarHeight = static_cast<int>(toolbar.bottom - toolbar.top);
    const int top = toolbar.top + std::max(0, (toolbarHeight - size) / 2);
    return RECT{
        .left = toolbar.left + left,
        .top = top,
        .right = toolbar.left + left + size,
        .bottom = top + size,
    };
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

    const RECT anchor = MenuRectByCommand(menu, openMenu);
    if (openMenu != EditorMenuCommand::None) {
        menu.dropdownRowCount = std::clamp(
            rowCount, 0, static_cast<int>(EditorMenuRects::MaximumRows));
        const int dropdownWidth = DropdownWidth(openMenu);
        const int dropX = std::min(std::max(rect.left + 4, anchor.left), std::max(rect.left + 4, rect.right - dropdownWidth - 4));
        const int dropY = rect.bottom + kDropdownTopGap;
        menu.dropdown = RECT{ dropX, dropY, dropX + dropdownWidth, dropY + (kDropdownRowHeight * menu.dropdownRowCount) };
        for (int i = 0; i < menu.dropdownRowCount; ++i) {
            menu.dropdownRows[static_cast<std::size_t>(i)] = RECT{
                menu.dropdown.left,
                menu.dropdown.top + (i * kDropdownRowHeight),
                menu.dropdown.right,
                menu.dropdown.top + ((i + 1) * kDropdownRowHeight),
            };
        }
    }
    return menu;
}

int EditorToolbarLayout::MenuTitlesRight(const RECT& rect) noexcept {
    return static_cast<int>(ResolveMenu(rect, EditorMenuCommand::None, 0).help.right);
}

// The toolbar shares the menu's row: the transport sits in the middle of the window (or
// just past the menus when the window is too narrow for that) and Save at the right end.
EditorToolbarRects EditorToolbarLayout::ResolveToolbar(const RECT& rect) noexcept {
    EditorToolbarRects toolbar{};
    toolbar.toolbar = rect;
    const int totalWidth = kTransportButtonSize * 3 + kTransportButtonGap * 2;
    const int toolbarWidth = static_cast<int>(rect.right - rect.left);
    const int left = std::max(static_cast<int>(rect.left) + std::max(0, (toolbarWidth - totalWidth) / 2),
        MenuTitlesRight(rect) + kMenuToTransportGap);
    toolbar.playButton = ButtonRect(rect, left - rect.left, kTransportButtonSize);
    toolbar.pauseButton = ButtonRect(rect, left - rect.left + kTransportButtonSize + kTransportButtonGap, kTransportButtonSize);
    toolbar.stopButton = ButtonRect(rect, left - rect.left + (kTransportButtonSize + kTransportButtonGap) * 2, kTransportButtonSize);
    const int saveLeft = static_cast<int>(rect.right) - kToolbarSideInset - kSaveButtonWidth;
    if (saveLeft >= toolbar.stopButton.right + kTransportButtonGap * 2) {
        toolbar.saveButton = ToolbarRect(rect, saveLeft - rect.left, kSaveButtonWidth, kSaveButtonHeight);
    }
    return toolbar;
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

int EditorToolbarLayout::MenuIndex(EditorMenuCommand menu) noexcept {
    switch (menu) {
    case EditorMenuCommand::File:
        return 0;
    case EditorMenuCommand::Edit:
        return 1;
    case EditorMenuCommand::Layout:
        return 2;
    case EditorMenuCommand::Options:
        return 3;
    case EditorMenuCommand::World:
        return 4;
    case EditorMenuCommand::Help:
        return 5;
    case EditorMenuCommand::None:
    default:
        return -1;
    }
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
    case EditorMenuCommand::None:
    default:
        return RECT{};
    }
}

int EditorToolbarLayout::FixedRowCount(EditorMenuCommand menu) noexcept {
    const int index = MenuIndex(menu);
    return index < 0 ? 0 : kDropdownRowCounts[static_cast<std::size_t>(index)];
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

// The Layout menu is missing here on purpose: its rows are the layouts a project
// holds, so they are built when the menu opens rather than fixed by the build.
std::string_view EditorToolbarLayout::DropdownLabel(EditorMenuCommand menu, int row) {
    if (MenuIndex(menu) < 0 || row < 0 || row >= FixedRowCount(menu)) {
        return {};
    }
    switch (menu) {
    case EditorMenuCommand::File: {
        const std::array<std::string_view, 5> rows{
            EditorText("menu.file.newScene", "New Scene..."),
            EditorText("menu.file.openScene", "Open Scene..."),
            EditorText("common.save", "Save"),
            EditorText("menu.file.saveAs", "Save As..."),
            EditorText("menu.file.buildGame", "Build Game"),
        };
        return rows[static_cast<std::size_t>(row)];
    }
    case EditorMenuCommand::Edit: {
        const std::array<std::string_view, 3> rows{
            EditorText("menu.edit.undo", "Undo"),
            EditorText("menu.edit.redo", "Redo"),
            EditorText("menu.edit.duplicate", "Duplicate"),
        };
        return rows[static_cast<std::size_t>(row)];
    }
    case EditorMenuCommand::Options: {
        const std::array<std::string_view, 6> rows{
            EditorText("menu.options.renderer", "Renderer"),
            EditorText("menu.options.projectSettings", "Project Settings"),
            EditorText("menu.options.editorSettings", "Editor Settings"),
            EditorText("menu.edit.plugins", "Plugins"),
            EditorText("panel.profiler", "Profiler"),
            EditorText("inspector.audioMixer.subtitle", "Audio Mixer"),
        };
        return rows[static_cast<std::size_t>(row)];
    }
    case EditorMenuCommand::World: {
        const std::array<std::string_view, 11> rows{
            EditorText("menu.world.loadCellsNearCamera", "Load Cells Near Camera"),
            EditorText("menu.world.loadAllCells", "Load All Cells"),
            EditorText("menu.world.unloadAllCells", "Unload All Cells"),
            EditorText("menu.world.buildWorldAndHlods", "Build World and HLODs"),
            EditorText("menu.world.toggleCellGrid", "Show or Hide Cell Grid"),
            EditorText("menu.world.convertSceneToWorld", "Convert Scene to World"),
            EditorText("menu.world.addDataLayer", "Add Data Layer..."),
            EditorText("menu.world.cycleSelectionDataLayer", "Cycle Selection Data Layer"),
            EditorText("menu.world.toggleSelectionAlwaysLoaded", "Toggle Selection Always Loaded"),
            EditorText("menu.world.bakeNavigationMesh", "Bake Navigation Mesh"),
            EditorText("menu.world.toggleNavigationMesh", "Show or Hide Navigation Mesh"),
        };
        return rows[static_cast<std::size_t>(row)];
    }
    case EditorMenuCommand::Help: {
        const std::array<std::string_view, 4> rows{
            EditorText("menu.help.documentation", "Documentation"),
            EditorText("menu.help.reportIssue", "Report Issue"),
            EditorText("menu.help.releaseNotes", "Release Notes"),
            EditorText("menu.help.about", "About"),
        };
        return rows[static_cast<std::size_t>(row)];
    }
    case EditorMenuCommand::Layout:
    case EditorMenuCommand::None:
    default:
        return {};
    }
}

HeroIconKind EditorToolbarLayout::DropdownIcon(EditorMenuCommand menu, int row) noexcept {
    if (MenuIndex(menu) < 0 || row < 0 || row >= FixedRowCount(menu)) {
        return HeroIconKind::RectangleGroup;
    }
    const auto pick = [row](auto icons) noexcept { return icons[static_cast<std::size_t>(row)]; };
    switch (menu) {
    case EditorMenuCommand::File:
        return pick(std::array{ HeroIconKind::Plus, HeroIconKind::Folder, HeroIconKind::Save,
            HeroIconKind::DocumentText, HeroIconKind::RocketLaunch });
    case EditorMenuCommand::Edit:
        return pick(std::array{ HeroIconKind::Undo, HeroIconKind::Redo, HeroIconKind::Duplicate });
    case EditorMenuCommand::Options:
        return pick(std::array{ HeroIconKind::Monitor, HeroIconKind::AdjustmentsHorizontal,
            HeroIconKind::WrenchScrewdriver, HeroIconKind::Plug, HeroIconKind::Activity, HeroIconKind::SpeakerWave });
    case EditorMenuCommand::World:
        return pick(std::array{ HeroIconKind::Camera, HeroIconKind::Download, HeroIconKind::Upload,
            HeroIconKind::WrenchScrewdriver, HeroIconKind::Grid, HeroIconKind::Map,
            HeroIconKind::Plus, HeroIconKind::Layers, HeroIconKind::LockClosed,
            HeroIconKind::Navigation, HeroIconKind::Eye });
    case EditorMenuCommand::Help:
        return pick(std::array{ HeroIconKind::BookOpen, HeroIconKind::Flag, HeroIconKind::DocumentText, HeroIconKind::Info });
    case EditorMenuCommand::Layout:
    case EditorMenuCommand::None:
    default:
        return HeroIconKind::RectangleGroup;
    }
}

// Only the shortcuts the editor actually binds (EditorEditCommandInputHandler); a row
// without one shows nothing rather than a key that does not work.
std::string_view EditorToolbarLayout::DropdownShortcut(EditorMenuCommand menu, int row) noexcept {
    if (menu == EditorMenuCommand::File && row == 2) {
        return "Ctrl+S";
    }
    if (menu == EditorMenuCommand::Edit) {
        switch (row) {
        case 0:
            return "Ctrl+Z";
        case 1:
            return "Ctrl+Y";
        case 2:
            return "Ctrl+D";
        default:
            break;
        }
    }
    return {};
}

// The long fixed menus read in groups: File's scene rows apart from Build Game, the
// project, editor and plugin settings apart from the tools, and World's streaming,
// build, data layer and navigation commands.
bool EditorToolbarLayout::DropdownStartsGroup(EditorMenuCommand menu, int row) noexcept {
    switch (menu) {
    case EditorMenuCommand::File:
        return row == 2 || row == 4;
    case EditorMenuCommand::Options:
        return row == 1 || row == 4;
    case EditorMenuCommand::World:
        return row == 3 || row == 6 || row == 9;
    case EditorMenuCommand::Help:
        return row == 3;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
