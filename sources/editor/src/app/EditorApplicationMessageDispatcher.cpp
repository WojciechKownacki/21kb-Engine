#include "app/EditorApplicationMessageDispatcher.hpp"

#if defined(_WIN32)
#include "app/EditorWindowMessageRouter.hpp"

namespace kb::editor {

LRESULT EditorApplicationMessageDispatcher::Dispatch(EditorApplicationState& state, HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    return EditorWindowMessageRouter{ EditorWindowMessageContext{
        state.window,
        state.running,
        state.dockModel,
        state.sceneContext,
        state.theme,
        state.metrics,
        state.renderer,
        state.floatingWindows,
        state.dockController,
        state.hierarchySelection,
    } }.Handle(messageWindow, message, wparam, lparam);
}

} // namespace kb::editor

#endif
