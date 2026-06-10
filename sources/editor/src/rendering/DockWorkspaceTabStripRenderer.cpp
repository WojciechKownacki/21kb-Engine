#include "rendering/DockWorkspaceTabStripRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kTabFontSize = 12;
constexpr int kTabTextVerticalOffset = 1;
constexpr int kTabIconSize = 15;
constexpr int kTabIconGap = 6;
constexpr int kTabHorizontalPadding = 9;
constexpr ULONGLONG kTabSlideAnimationMs = 150;

struct TabSlideAnimation {
    RECT start{};
    RECT target{};
    ULONGLONG startTick = 0;
    bool initialized = false;
    bool animating = false;
};

struct LeafTabOrderState {
    std::vector<std::uint32_t> order;
    bool initialized = false;
};

struct RenderedTab {
    DockPanelLayout layout{};
    const DockPanel* panel = nullptr;
};

void DrawHairline(HDC dc, const RECT& rect, COLORREF color) {
    GdiDrawing::FillRectColor(dc, rect, color);
}

void DrawTabChrome(HDC dc, const RECT& tab, bool active, const EditorTheme& theme) {
    const COLORREF fill = GdiDrawing::ToColorRef(active ? theme.tabActive : theme.strip);
    GdiDrawing::FillRectColor(dc, tab, fill);

    const COLORREF edge = GdiDrawing::ToColorRef(active ? theme.borderPanel : theme.chrome);
    if (active) {
        DrawHairline(dc, RECT{ tab.left, tab.top, tab.right, tab.top + 1 }, edge);
        DrawHairline(dc, RECT{ tab.left, tab.top, tab.left + 1, tab.bottom }, edge);
    }
    DrawHairline(dc, RECT{ tab.right - 1, tab.top + 1, tab.right, tab.bottom - 1 }, edge);
}

[[nodiscard]] RECT TabTitleRect(const DockRect& tab) {
    RECT titleRect = GdiDrawing::ToRect(tab);
    titleRect.left += 8;
    titleRect.right -= 8;
    titleRect.top += kTabTextVerticalOffset;
    return titleRect;
}

[[nodiscard]] bool TabIconForPanel(DockPanelKind kind, HeroIconKind& icon) noexcept {
    switch (kind) {
    case DockPanelKind::Hierarchy:
        icon = HeroIconKind::ListBullet;
        return true;
    case DockPanelKind::Scene:
        icon = HeroIconKind::Cube;
        return true;
    case DockPanelKind::Inspector:
        icon = HeroIconKind::AdjustmentsHorizontal;
        return true;
    case DockPanelKind::Assets:
        icon = HeroIconKind::Folder;
        return true;
    case DockPanelKind::Console:
        icon = HeroIconKind::CommandLine;
        return true;
    case DockPanelKind::ProjectSettings:
        icon = HeroIconKind::Gamepad2;
        return true;
    case DockPanelKind::ScriptEditor:
        icon = HeroIconKind::CommandLine;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] int MeasureTextWidth(HDC dc, const char* text) noexcept {
    SIZE size{};
    return GetTextExtentPoint32A(dc, text, static_cast<int>(std::strlen(text)), &size) ? size.cx : 0;
}

void DrawTabLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void DrawTabContent(HDC dc, const DockPanelLayout& panelLayout, const DockPanel& panel, const EditorTheme& theme) {
    HeroIconKind icon{};
    if (!TabIconForPanel(panel.kind, icon)) {
        RECT titleRect = TabTitleRect(panelLayout.tab);
        GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary));
        return;
    }

    const RECT tab = GdiDrawing::ToRect(panelLayout.tab);
    const int tabWidth = std::max(0, static_cast<int>(tab.right - tab.left));
    const int textWidth = MeasureTextWidth(dc, panel.title.c_str());
    const int maxTextWidth = std::max(0, tabWidth - (kTabHorizontalPadding * 2) - kTabIconSize - kTabIconGap);
    const int visibleTextWidth = std::min(textWidth, maxTextWidth);
    const int groupWidth = kTabIconSize + kTabIconGap + visibleTextWidth;
    const int groupLeft = tab.left + std::max(kTabHorizontalPadding, (tabWidth - groupWidth) / 2);
    const int iconTop = tab.top + ((tab.bottom - tab.top - kTabIconSize) / 2) + kTabTextVerticalOffset;

    const COLORREF color = GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary);
    const RECT iconRect{ groupLeft, iconTop, groupLeft + kTabIconSize, iconTop + kTabIconSize };
    HeroIconPainter::Draw(dc, iconRect, icon, color, icon == HeroIconKind::Gamepad2 ? 2 : 1);

    RECT textRect{ iconRect.right + kTabIconGap, tab.top + kTabTextVerticalOffset, tab.right - kTabHorizontalPadding, tab.bottom };
    DrawTabLabel(dc, textRect, panel.title.c_str(), color);
}

[[nodiscard]] bool SameRect(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

[[nodiscard]] int LerpInt(int start, int target, float t) noexcept {
    return static_cast<int>(static_cast<float>(start) + ((static_cast<float>(target - start)) * t) + 0.5F);
}

[[nodiscard]] float TabSlideProgress(ULONGLONG now, ULONGLONG start) noexcept {
    if (now <= start) {
        return 0.0F;
    }
    const float raw = std::min(1.0F, static_cast<float>(now - start) / static_cast<float>(kTabSlideAnimationMs));
    return raw * raw * (3.0F - (2.0F * raw));
}

[[nodiscard]] RECT AnimatedRect(const TabSlideAnimation& animation, ULONGLONG now) noexcept {
    if (!animation.animating) {
        return animation.target;
    }
    const float progress = TabSlideProgress(now, animation.startTick);
    return RECT{
        .left = LerpInt(animation.start.left, animation.target.left, progress),
        .top = LerpInt(animation.start.top, animation.target.top, progress),
        .right = LerpInt(animation.start.right, animation.target.right, progress),
        .bottom = LerpInt(animation.start.bottom, animation.target.bottom, progress),
    };
}

[[nodiscard]] DockRect ToDockRect(const RECT& rect) noexcept {
    return DockRect{
        .x = rect.left,
        .y = rect.top,
        .width = rect.right - rect.left,
        .height = rect.bottom - rect.top,
    };
}

[[nodiscard]] DockRect ResolveAnimatedTabRect(HWND owner, const DockPanelLayout& panelLayout, bool allowAnimation, bool orderChanged) {
    static std::unordered_map<std::uint32_t, TabSlideAnimation> animations;

    const ULONGLONG now = GetTickCount64();
    const RECT target = GdiDrawing::ToRect(panelLayout.tab);
    TabSlideAnimation& animation = animations[panelLayout.panelId];
    if (!animation.initialized) {
        animation = TabSlideAnimation{
            .start = target,
            .target = target,
            .startTick = now,
            .initialized = true,
            .animating = false,
        };
        return ToDockRect(target);
    }

    if (!allowAnimation) {
        if (!SameRect(animation.target, target) || animation.animating) {
            animation.start = target;
            animation.target = target;
            animation.startTick = now;
            animation.animating = false;
        }
        return ToDockRect(target);
    }

    const RECT current = AnimatedRect(animation, now);
    if (!orderChanged && !SameRect(animation.target, target)) {
        animation.start = target;
        animation.target = target;
        animation.startTick = now;
        animation.animating = false;
        return ToDockRect(target);
    }

    if (orderChanged && !SameRect(animation.target, target)) {
        animation.start = current;
        animation.target = target;
        animation.startTick = now;
        animation.animating = true;
        if (owner != nullptr) {
            InvalidateRect(owner, nullptr, FALSE);
        }
        return ToDockRect(current);
    }

    if (!animation.animating) {
        return ToDockRect(animation.target);
    }

    const float progress = TabSlideProgress(now, animation.startTick);
    if (progress >= 1.0F) {
        animation.animating = false;
        animation.start = animation.target;
        return ToDockRect(animation.target);
    }

    if (owner != nullptr) {
        InvalidateRect(owner, nullptr, FALSE);
    }
    return ToDockRect(current);
}

[[nodiscard]] std::unordered_map<std::uint32_t, bool> ResolveAnimatedLeaves(const DockLayout& layout) {
    static std::unordered_map<std::uint32_t, LeafTabOrderState> leafStates;

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> currentOrders;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        currentOrders[panelLayout.leafId].push_back(panelLayout.panelId);
    }

    std::unordered_map<std::uint32_t, bool> animatedLeaves;
    for (const auto& [leafId, order] : currentOrders) {
        LeafTabOrderState& state = leafStates[leafId];
        const bool orderChanged = state.initialized && state.order != order;
        animatedLeaves[leafId] = orderChanged;
        state.order = order;
        state.initialized = true;
    }
    return animatedLeaves;
}

[[nodiscard]] bool AllowsManualTabSlide(const DockPointerDrag* dockDrag, std::uint32_t leafId) noexcept {
    return dockDrag != nullptr
        && dockDrag->kind == DockHitKind::Tab
        && dockDrag->manualTabDrag
        && !dockDrag->detached
        && dockDrag->sourceLeafId == leafId;
}

void PaintRenderedTab(HDC dc, const RenderedTab& tab, const EditorTheme& theme) {
    DrawTabChrome(dc, GdiDrawing::ToRect(tab.layout.tab), tab.layout.active, theme);
    DrawTabContent(dc, tab.layout, *tab.panel, theme);
}

} // namespace

void DockWorkspaceTabStripRenderer::Paint(HWND owner, HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const DockPointerDrag* dockDrag, const EditorTheme& theme) const {
    ScopedFont titleFont(kTabFontSize, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, titleFont.handle);
    std::vector<RenderedTab> inactiveTabs;
    std::vector<RenderedTab> activeTabs;
    const std::unordered_map<std::uint32_t, bool> animatedLeaves = ResolveAnimatedLeaves(layout);

    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        DockPanelLayout animatedLayout = panelLayout;
        const auto animateLeaf = animatedLeaves.find(panelLayout.leafId);
        const bool orderChanged = animateLeaf != animatedLeaves.end() && animateLeaf->second;
        animatedLayout.tab = ResolveAnimatedTabRect(owner, panelLayout, AllowsManualTabSlide(dockDrag, panelLayout.leafId), orderChanged);
        RenderedTab rendered{
            .layout = animatedLayout,
            .panel = panel,
        };
        if (animatedLayout.active) {
            activeTabs.push_back(rendered);
        } else {
            inactiveTabs.push_back(rendered);
        }
    }

    for (const RenderedTab& tab : inactiveTabs) {
        PaintRenderedTab(dc, tab, theme);
    }
    for (const RenderedTab& tab : activeTabs) {
        PaintRenderedTab(dc, tab, theme);
    }
}

} // namespace kb::editor

#endif
