#include "docking/EditorFloatingWindowQueries.hpp"

#if defined(_WIN32)
#include "docking/EditorDockModel.hpp"
#include "docking/FloatingWindowRectReader.hpp"
#include "windowing/FloatingWindowHitTestResolver.hpp"

namespace kb::editor {

EditorFloatingWindowQueries::EditorFloatingWindowQueries(const FloatingWindowRegistry& registry, const EditorMetrics* metrics, const EditorDockModel* dockModel) noexcept
    : registry_(registry)
    , metrics_(metrics)
    , dockModel_(dockModel) {}

bool EditorFloatingWindowQueries::IsFloatingWindow(HWND window) const noexcept {
    return registry_.ContainsWindow(window);
}

std::uint32_t EditorFloatingWindowQueries::GroupId(HWND window) const noexcept {
    return registry_.GroupId(window);
}

std::uint32_t EditorFloatingWindowQueries::PanelId(HWND window) const noexcept {
    const DockNode* group = dockModel_ != nullptr ? dockModel_->Queries().FloatingGroup(registry_.GroupId(window)) : nullptr;
    return group != nullptr ? group->activePanelId : 0U;
}

HWND EditorFloatingWindowQueries::WindowForPanel(std::uint32_t panelId) const noexcept {
    return dockModel_ != nullptr ? registry_.WindowForGroup(dockModel_->Queries().FloatingGroupOfPanel(panelId)) : nullptr;
}

std::vector<HWND> EditorFloatingWindowQueries::Windows() const {
    return registry_.Windows();
}

LRESULT EditorFloatingWindowQueries::HitTest(HWND window, LPARAM lparam) const {
    if (metrics_ == nullptr) {
        return HTCLIENT;
    }

    return FloatingWindowHitTestResolver::Resolve(window, lparam, *metrics_);
}

std::optional<FloatingWindowResizeEvent> EditorFloatingWindowQueries::ResizeEvent(HWND window, int width, int height) const noexcept {
    return FloatingWindowResizeEventResolver::Resolve(PanelId(window), width, height);
}

std::optional<DockRect> EditorFloatingWindowQueries::RectForPanel(std::uint32_t panelId) const {
    return FloatingWindowRectReader::Read(WindowForPanel(panelId));
}

} // namespace kb::editor

#endif
