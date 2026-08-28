#include "rendering/EditorToolbarLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kTransportButtonSize = 30;
constexpr int kTransportButtonGap = 5;
constexpr int kToolbarSideInset = 12;
constexpr int kSaveButtonWidth = 60;
constexpr int kSaveButtonMinWidth = 50;
constexpr int kSaveButtonHeight = 26;
constexpr int kMenuLeftInset = 10;
constexpr int kMenuTopInset = 2;
constexpr int kMenuItemHeightPad = 3;
constexpr int kDropdownTopGap = 4;
constexpr int kDropdownWidth = 230;
constexpr int kDropdownRowHeight = 30;

constexpr std::array<EditorMenuDescriptor, 4> kMenus{{
    { EditorMenuCommand::File, "File", 54 },
    { EditorMenuCommand::Edit, "Edit", 54 },
    { EditorMenuCommand::Options, "Options", 82 },
    { EditorMenuCommand::Help, "Help", 58 },
}};

constexpr std::array<std::array<std::string_view, 4>, 4> kDropdownRows{{
    { "New Scene", "Open Scene...", "Save", "Save As..." },
    { "Undo", "Redo", "Duplicate", "Plugins" },
    { "Renderer", "Reset Layout", "Project Settings", "Editor Settings" },
    { "Documentation", "Report Issue", "Release Notes", "About" },
}};

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

const std::array<EditorMenuDescriptor, 4>& EditorToolbarLayout::MenuDescriptors() noexcept {
    return kMenus;
}

EditorMenuRects EditorToolbarLayout::ResolveMenu(const RECT& rect, EditorMenuCommand openMenu) noexcept {
    EditorMenuRects menu{};
    menu.menuBar = rect;
    int left = rect.left + kMenuLeftInset;
    for (const EditorMenuDescriptor& descriptor : kMenus) {
        RECT item{
            .left = left,
            .top = rect.top + kMenuTopInset,
            .right = left + descriptor.width,
            .bottom = rect.bottom - kMenuItemHeightPad,
        };
        switch (descriptor.command) {
        case EditorMenuCommand::File:
            menu.file = item;
            break;
        case EditorMenuCommand::Edit:
            menu.edit = item;
            break;
        case EditorMenuCommand::Options:
            menu.options = item;
            break;
        case EditorMenuCommand::Help:
            menu.help = item;
            break;
        case EditorMenuCommand::None:
        default:
            break;
        }
        left += descriptor.width;
    }

    const RECT anchor = MenuRectByCommand(menu, openMenu);
    if (openMenu != EditorMenuCommand::None) {
        const int dropX = std::min(std::max(rect.left + 4, anchor.left), std::max(rect.left + 4, rect.right - kDropdownWidth - 4));
        const int dropY = rect.bottom + kDropdownTopGap;
        menu.dropdown = RECT{ dropX, dropY, dropX + kDropdownWidth, dropY + (kDropdownRowHeight * 4) };
        for (int i = 0; i < 4; ++i) {
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

EditorToolbarRects EditorToolbarLayout::ResolveToolbar(const RECT& rect) noexcept {
    EditorToolbarRects toolbar{};
    toolbar.toolbar = rect;
    const int totalWidth = kTransportButtonSize * 3 + kTransportButtonGap * 2;
    const int toolbarWidth = static_cast<int>(rect.right - rect.left);
    const int left = rect.left + std::max(0, (toolbarWidth - totalWidth) / 2);
    toolbar.playButton = ButtonRect(rect, left - rect.left, kTransportButtonSize);
    toolbar.pauseButton = ButtonRect(rect, left - rect.left + kTransportButtonSize + kTransportButtonGap, kTransportButtonSize);
    toolbar.stopButton = ButtonRect(rect, left - rect.left + (kTransportButtonSize + kTransportButtonGap) * 2, kTransportButtonSize);
    const int saveAvailableWidth = toolbar.playButton.left - rect.left - kTransportButtonGap - kToolbarSideInset;
    if (saveAvailableWidth >= kSaveButtonMinWidth) {
        toolbar.saveButton = ToolbarRect(rect, kToolbarSideInset, std::min(kSaveButtonWidth, saveAvailableWidth), kSaveButtonHeight);
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
    if (PointInRect(rects.options, x, y)) {
        return EditorMenuCommand::Options;
    }
    if (PointInRect(rects.help, x, y)) {
        return EditorMenuCommand::Help;
    }
    return EditorMenuCommand::None;
}

std::optional<int> EditorToolbarLayout::HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept {
    for (std::size_t i = 0; i < rects.dropdownRows.size(); ++i) {
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
    case EditorMenuCommand::Options:
        return 2;
    case EditorMenuCommand::Help:
        return 3;
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
    case EditorMenuCommand::Options:
        return rects.options;
    case EditorMenuCommand::Help:
        return rects.help;
    case EditorMenuCommand::None:
    default:
        return RECT{};
    }
}

std::string_view EditorToolbarLayout::DropdownLabel(EditorMenuCommand menu, int row) noexcept {
    const int index = MenuIndex(menu);
    if (index < 0 || row < 0 || row >= 4) {
        return {};
    }
    return kDropdownRows[static_cast<std::size_t>(index)][static_cast<std::size_t>(row)];
}

} // namespace kb::editor

#endif
