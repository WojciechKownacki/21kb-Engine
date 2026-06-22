#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

enum class DockArea : std::uint8_t {
    Left,
    Center,
    Right,
    Bottom,
    Floating,
};

enum class DockPanelKind : std::uint8_t {
    Generic,
    Hierarchy,
    Scene,
    Inspector,
    Assets,
    Console,
    ProjectSettings,
    ScriptEditor,
    Plugins,
    MaterialEditor,
};

enum class DockSplitAxis : std::uint8_t {
    Horizontal,
    Vertical,
};

enum class DockDropZone : std::uint8_t {
    None,
    Center,
    Left,
    Right,
    Top,
    Bottom,
};

enum class DockHitKind : std::uint8_t {
    None,
    Tab,
    Splitter,
};

enum class DockDropPreviewKind : std::uint8_t {
    Glow,
    StripMarker,
};

struct DockRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool Contains(int px, int py) const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
};

struct DockPanel {
    std::uint32_t id = 0;
    DockPanelKind kind = DockPanelKind::Generic;
    std::string title;
    DockArea area = DockArea::Center;
    bool visible = true;
    bool detachable = true;
    DockRect floatingRect{};
};

struct DockPanelLayout {
    std::uint32_t panelId = 0;
    std::uint32_t leafId = 0;
    DockRect frame{};
    DockRect tabStrip{};
    DockRect tab{};
    DockRect content{};
    DockRect contentClip{};
    bool active = false;
};

struct DockLeafLayout {
    std::uint32_t leafId = 0;
    DockRect frame{};
    DockRect tabStrip{};
    DockRect content{};
    std::uint32_t activePanelId = 0;
};

struct DockSplitterLayout {
    std::uint32_t nodeId = 0;
    DockSplitAxis axis = DockSplitAxis::Horizontal;
    DockRect rect{};
    DockRect container{};
};

struct DockLayout {
    DockRect menu{};
    DockRect toolbar{};
    DockRect workspace{};
    std::vector<DockLeafLayout> leaves{};
    std::vector<DockPanelLayout> panels{};
    std::vector<DockSplitterLayout> splitters{};
};

struct DockHit {
    DockHitKind kind = DockHitKind::None;
    std::uint32_t panelId = 0;
    std::uint32_t leafId = 0;
    std::uint32_t splitterNodeId = 0;
};

struct DockDropPreview {
    DockDropZone zone = DockDropZone::None;
    DockDropPreviewKind kind = DockDropPreviewKind::Glow;
    std::uint32_t leafId = 0;
    DockRect rect{};
    std::uint32_t tabInsertionIndex = 0;
};

} // namespace kb::editor
