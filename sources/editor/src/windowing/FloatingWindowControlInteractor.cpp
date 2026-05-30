#include "windowing/FloatingWindowControlInteractor.hpp"

#if defined(_WIN32)
#include "windowing/FloatingWindowControlHitTester.hpp"

namespace kb::editor {

bool FloatingWindowControlInteractor::HandlePointerDown(HWND window, const EditorMetrics& metrics, int x, int y) const {
    RECT client{};
    GetClientRect(window, &client);
    const FloatingWindowControlKind control = FloatingWindowControlHitTester{}.HitTest(metrics, client.right - client.left, x, y);
    if (control == FloatingWindowControlKind::None) {
        return false;
    }

    ReleaseCapture();
    return FloatingWindowControlActions{}.Execute(window, control);
}

} // namespace kb::editor

#endif
