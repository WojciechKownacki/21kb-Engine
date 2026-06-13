#include "assets/EditorAssetBrowserOverlayHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
#include <iterator>

namespace kb::editor {
namespace {

constexpr int kLightingSubmenuWidth = 190;
constexpr int kLightingSubmenuRows = 4;
constexpr int kLightingSubmenuGap = 4;

[[nodiscard]] bool IsLightingSubmenuCommand(EditorAssetContextCommand command) noexcept {
    return command == EditorAssetContextCommand::AddLighting
        || command == EditorAssetContextCommand::AddDirectionalLight
        || command == EditorAssetContextCommand::AddPointLight
        || command == EditorAssetContextCommand::AddSpotLight;
}

[[nodiscard]] std::optional<int> AddLightingIndex(const std::vector<EditorAssetContextMenuItem>& items) {
    const auto iter = std::ranges::find_if(items, [](const EditorAssetContextMenuItem& item) {
        return item.command == EditorAssetContextCommand::AddLighting;
    });
    if (iter == items.end()) {
        return std::nullopt;
    }
    return static_cast<int>(std::distance(items.begin(), iter));
}

[[nodiscard]] bool HasLightingSubmenu(const std::vector<EditorAssetContextMenuItem>& items) noexcept {
    return std::ranges::any_of(items, [](const EditorAssetContextMenuItem& item) {
        return item.command == EditorAssetContextCommand::AddLighting;
    });
}

[[nodiscard]] RECT ContextMenuRectForItems(const RECT& content, int x, int y, const std::vector<EditorAssetContextMenuItem>& items) noexcept {
    return EditorAssetBrowserLayout::ContextMenuRect(content, x, y, static_cast<int>(items.size()));
}

[[nodiscard]] RECT LightingSubmenuRect(const RECT& content, const RECT& menu, const std::vector<EditorAssetContextMenuItem>& items) {
    const std::optional<int> addIndex = AddLightingIndex(items);
    if (!addIndex.has_value()) {
        return {};
    }
    const RECT addRow = EditorAssetBrowserLayout::ContextMenuItemRect(menu, *addIndex);
    RECT submenu{
        addRow.right + kLightingSubmenuGap,
        addRow.top,
        addRow.right + kLightingSubmenuGap + kLightingSubmenuWidth,
        addRow.top + EditorAssetBrowserLayout::ContextMenuPadding * 2
            + kLightingSubmenuRows * EditorAssetBrowserLayout::ContextMenuRowHeight
            + (kLightingSubmenuRows - 1) * EditorAssetBrowserLayout::ContextMenuSeparatorHeight,
    };
    if (submenu.right > content.right) {
        const int width = submenu.right - submenu.left;
        const int height = submenu.bottom - submenu.top;
        const int left = std::clamp(static_cast<int>(menu.left), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
        int top = menu.bottom + kLightingSubmenuGap;
        if (top + height > content.bottom) {
            top = menu.top - height - kLightingSubmenuGap;
        }
        top = std::clamp(top, static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
        submenu = RECT{ left, top, left + width, top + height };
    } else if (submenu.bottom > content.bottom) {
        OffsetRect(&submenu, 0, content.bottom - submenu.bottom);
    }
    return submenu;
}

[[nodiscard]] EditorAssetContextCommand LightingCommandAt(const RECT& submenu, int x, int y) noexcept {
    if (!EditorAssetBrowserGeometry::Contains(submenu, x, y)) {
        return EditorAssetContextCommand::None;
    }
    for (int index = 1; index < kLightingSubmenuRows; ++index) {
        if (!EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::ContextMenuItemRect(submenu, index), x, y)) {
            continue;
        }
        switch (index) {
        case 1:
            return EditorAssetContextCommand::AddDirectionalLight;
        case 2:
            return EditorAssetContextCommand::AddPointLight;
        case 3:
            return EditorAssetContextCommand::AddSpotLight;
        default:
            return EditorAssetContextCommand::None;
        }
    }
    return EditorAssetContextCommand::AddLighting;
}

[[nodiscard]] bool InLightingSubmenuBridge(const RECT& menu, const RECT& submenu, const std::vector<EditorAssetContextMenuItem>& items, int x, int y) {
    const std::optional<int> addIndex = AddLightingIndex(items);
    if (!addIndex.has_value()) {
        return false;
    }
    const RECT addRow = EditorAssetBrowserLayout::ContextMenuItemRect(menu, *addIndex);
    RECT bridge{};
    if (submenu.left >= addRow.right) {
        bridge = RECT{ addRow.right, addRow.top, submenu.left, addRow.bottom };
    } else if (submenu.top >= addRow.bottom) {
        bridge = RECT{ addRow.left, addRow.bottom, addRow.right, submenu.top };
    } else if (submenu.bottom <= addRow.top) {
        bridge = RECT{ addRow.left, submenu.bottom, addRow.right, addRow.top };
    } else {
        return false;
    }
    return EditorAssetBrowserGeometry::Contains(bridge, x, y);
}

} // namespace

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    const RECT* overlayBounds) {
    if (!state.IsDeleteConfirmOpen()) {
        return std::nullopt;
    }

    const RECT bounds = overlayBounds != nullptr ? *overlayBounds : content;
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(bounds, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(dialog), x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmAccept };
    }
    if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::DeleteConfirmCancelRect(dialog), x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmCancel };
    }
    const RECT list = EditorAssetBrowserGeometry::DeleteConfirmListRect(bounds, state);
    if (EditorAssetBrowserGeometry::DeleteConfirmListMaxScroll(bounds, state, manager) > 0) {
        const RECT track = EditorAssetBrowserGeometry::DeleteConfirmListScrollbarTrackRect(bounds, state);
        const RECT thumb = EditorAssetBrowserGeometry::DeleteConfirmListScrollbarThumbRect(bounds, state, manager);
        if (EditorAssetBrowserGeometry::Contains(thumb, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmScrollbarThumb };
        }
        if (EditorAssetBrowserGeometry::Contains(track, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmScrollbarTrack };
        }
    }
    if (EditorAssetBrowserGeometry::Contains(list, x, y)) {
        if (const std::optional<std::size_t> row = EditorAssetBrowserGeometry::DeleteConfirmListRowAt(bounds, state, manager, x, y)) {
            if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::DeleteConfirmListCheckboxRect(bounds, state, *row), x, y)) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmCheckbox, .index = *row };
            }
        }
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmListBody };
    }
    return EditorAssetBrowserGeometry::Contains(dialog, x, y)
        ? EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmBody }
        : EditorAssetBrowserHit{};
}

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestContextMenu(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    if (!state.IsContextMenuOpen()) {
        return std::nullopt;
    }

    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(manager);
    if (items.empty()) {
        return std::nullopt;
    }

    const RECT menu = ContextMenuRectForItems(content, state.ContextMenuX(), state.ContextMenuY(), items);
    if (IsLightingSubmenuCommand(state.ContextMenuHoveredCommand())) {
        const RECT submenu = LightingSubmenuRect(content, menu, items);
        if (InLightingSubmenuBridge(menu, submenu, items, x, y)) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::ContextMenuCommand,
                .command = EditorAssetContextCommand::AddLighting,
            };
        }
        const EditorAssetContextCommand submenuCommand = LightingCommandAt(submenu, x, y);
        if (submenuCommand != EditorAssetContextCommand::None) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::ContextMenuCommand,
                .command = submenuCommand,
            };
        }
    }

    if (!EditorAssetBrowserGeometry::Contains(menu, x, y)) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::ContextMenuItemRect(menu, static_cast<int>(index)), x, y)) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::ContextMenuCommand,
                .index = index,
                .command = items[index].command,
            };
        }
    }
    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContextMenuBody };
}

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestDropActionMenu(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    if (!state.IsDropActionMenuOpen()) {
        return std::nullopt;
    }

    const RECT menu = EditorAssetBrowserLayout::ContextMenuRect(content, state.DropActionMenuX(), state.DropActionMenuY(), 2);
    if (!EditorAssetBrowserGeometry::Contains(menu, x, y)) {
        return EditorAssetBrowserHit{};
    }

    for (int index = 0; index < 2; ++index) {
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::ContextMenuItemRect(menu, index), x, y)) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::DropActionCommand,
                .index = static_cast<std::size_t>(index),
                .dropAction = index == 0 ? EditorAssetDropAction::MoveHere : EditorAssetDropAction::CopyHere,
            };
        }
    }
    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropActionBody };
}

} // namespace kb::editor

#endif
