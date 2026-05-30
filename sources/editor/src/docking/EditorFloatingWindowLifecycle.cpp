#include "docking/EditorFloatingWindowLifecycle.hpp"

#if defined(_WIN32)
#include "docking/FloatingWindowDestroyer.hpp"

namespace kb::editor {

EditorFloatingWindowLifecycle::EditorFloatingWindowLifecycle(FloatingWindowRegistry& registry, HINSTANCE& instance, HWND& owner, const EditorMetrics*& metrics) noexcept
    : registry_(registry)
    , instance_(instance)
    , owner_(owner)
    , metrics_(metrics) {}

void EditorFloatingWindowLifecycle::Configure(HINSTANCE instance, HWND owner, const EditorMetrics& metrics) noexcept {
    instance_ = instance;
    owner_ = owner;
    metrics_ = &metrics;
}

void EditorFloatingWindowLifecycle::Shutdown() {
    FloatingWindowDestroyer::DestroyAll(registry_);
}

void EditorFloatingWindowLifecycle::OnDestroyed(HWND window) {
    const std::uint32_t panelId = registry_.PanelId(window);
    if (panelId == 0) {
        return;
    }

    registry_.RemoveWindow(window);
}

} // namespace kb::editor

#endif
