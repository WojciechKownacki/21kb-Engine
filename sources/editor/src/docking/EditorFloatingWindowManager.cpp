#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)

namespace kb::editor {

EditorFloatingWindowQueries EditorFloatingWindowManager::Queries() const noexcept {
    return EditorFloatingWindowQueries{ registry_, metrics_ };
}

EditorFloatingWindowCommands EditorFloatingWindowManager::Commands() noexcept {
    return EditorFloatingWindowCommands{ registry_, instance_, owner_ };
}

EditorFloatingWindowLifecycle EditorFloatingWindowManager::Lifecycle() noexcept {
    return EditorFloatingWindowLifecycle{ registry_, instance_, owner_, metrics_ };
}

} // namespace kb::editor

#endif
