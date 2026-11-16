#include "docking/EditorFloatingWindowCommands.hpp"

#if defined(_WIN32)
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "docking/FloatingWindowCreator.hpp"
#include "docking/FloatingWindowDestroyer.hpp"

namespace kb::editor {

EditorFloatingWindowCommands::EditorFloatingWindowCommands(FloatingWindowRegistry& registry, HINSTANCE instance, HWND owner, const EditorDockModel* dockModel) noexcept
    : registry_(registry)
    , instance_(instance)
    , owner_(owner)
    , dockModel_(dockModel) {}

bool EditorFloatingWindowCommands::Create(std::uint32_t panelId, const std::string& titleText, const DockRect& rect) {
    const std::uint32_t groupId = dockModel_ != nullptr ? dockModel_->Queries().FloatingGroupOfPanel(panelId) : 0U;
    return groupId != 0U && FloatingWindowCreator::Create(registry_, instance_, owner_, EditorFloatingWindowManager::WindowClassName, groupId, titleText, rect);
}

void EditorFloatingWindowCommands::DestroyOrphaned() {
    std::vector<std::uint32_t> liveGroups;
    if (dockModel_ != nullptr) {
        for (const DockNode& group : dockModel_->Queries().FloatingGroups()) {
            liveGroups.push_back(group.id);
        }
    }
    FloatingWindowDestroyer::DestroyOrphaned(registry_, liveGroups);
}

} // namespace kb::editor

#endif
