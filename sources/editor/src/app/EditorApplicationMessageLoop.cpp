#include "app/EditorApplicationMessageLoop.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorApplicationMessageLoop::Run(bool& running) {
    MSG message{};
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace kb::editor

#endif
