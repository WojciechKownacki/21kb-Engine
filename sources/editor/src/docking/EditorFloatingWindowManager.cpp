#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)

namespace kb::editor {

EditorFloatingWindowQueries EditorFloatingWindowManager::Queries() const noexcept {
    return EditorFloatingWindowQueries{ registry_, metrics_, dockModel_ };
}

EditorFloatingWindowCommands EditorFloatingWindowManager::Commands() noexcept {
    return EditorFloatingWindowCommands{ registry_, instance_, owner_, dockModel_ };
}

EditorFloatingWindowLifecycle EditorFloatingWindowManager::Lifecycle() noexcept {
    return EditorFloatingWindowLifecycle{ registry_, instance_, owner_, metrics_, dockModel_ };
}

} // namespace kb::editor

#endif
