#include "docking/EditorFloatingWindowLifecycle.hpp"

#if defined(_WIN32)
#include "docking/FloatingWindowDestroyer.hpp"

namespace kb::editor {

EditorFloatingWindowLifecycle::EditorFloatingWindowLifecycle(FloatingWindowRegistry& registry, HINSTANCE& instance, HWND& owner, const EditorMetrics*& metrics, const EditorDockModel*& dockModel) noexcept
    : registry_(registry)
    , instance_(instance)
    , owner_(owner)
    , metrics_(metrics)
    , dockModel_(dockModel) {}

void EditorFloatingWindowLifecycle::Configure(HINSTANCE instance, HWND owner, const EditorMetrics& metrics, const EditorDockModel& dockModel) noexcept {
    instance_ = instance;
    owner_ = owner;
    metrics_ = &metrics;
    dockModel_ = &dockModel;
}

void EditorFloatingWindowLifecycle::Shutdown() {
    FloatingWindowDestroyer::DestroyAll(registry_);
}

void EditorFloatingWindowLifecycle::OnDestroyed(HWND window) {
    registry_.RemoveWindow(window);
}

} // namespace kb::editor

#endif
