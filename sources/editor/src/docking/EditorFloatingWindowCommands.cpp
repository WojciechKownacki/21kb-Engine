#include "docking/EditorFloatingWindowCommands.hpp"

#if defined(_WIN32)
#include "docking/EditorFloatingWindowManager.hpp"
#include "docking/FloatingWindowCreator.hpp"
#include "docking/FloatingWindowDestroyer.hpp"

namespace kb::editor {

EditorFloatingWindowCommands::EditorFloatingWindowCommands(FloatingWindowRegistry& registry, HINSTANCE instance, HWND owner) noexcept
    : registry_(registry)
    , instance_(instance)
    , owner_(owner) {}

bool EditorFloatingWindowCommands::Create(std::uint32_t panelId, const std::string& titleText, const DockRect& rect) {
    return FloatingWindowCreator::Create(registry_, instance_, owner_, EditorFloatingWindowManager::WindowClassName, panelId, titleText, rect);
}

void EditorFloatingWindowCommands::Destroy(std::uint32_t panelId) {
    FloatingWindowDestroyer::DestroyPanel(registry_, panelId);
}

} // namespace kb::editor

#endif
