#include "docking/DockModelQueries.hpp"

#include "docking/DockDropPreviewResolver.hpp"
#include "docking/DockHitTester.hpp"
#include "docking/DockLayoutBuilder.hpp"

namespace kb::editor {

DockLayout DockModelQueries::BuildLayout(
    const DockNode* root,
    int clientWidth,
    int clientHeight,
    int menuHeight,
    int statusBarHeight,
    int tabStripHeight,
    int tabMinWidth,
    int tabWidth,
    int splitterSize) {
    return DockLayoutBuilder{}.Build(root, clientWidth, clientHeight, menuHeight, statusBarHeight, tabStripHeight, tabMinWidth, tabWidth, splitterSize);
}

DockHit DockModelQueries::HitTest(const DockLayout& layout, int x, int y) {
    return DockHitTester{}.HitTest(layout, x, y);
}

std::optional<DockDropPreview> DockModelQueries::ResolveDropPreview(const DockLayout& layout, int x, int y) {
    return DockDropPreviewResolver{}.Resolve(layout, x, y);
}

} // namespace kb::editor
