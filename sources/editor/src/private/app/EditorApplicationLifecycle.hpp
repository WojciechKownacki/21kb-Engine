#pragma once

#include "app/EditorApplicationState.hpp"

namespace kb::editor {

class EditorApplicationLifecycle {
public:
    EditorApplicationLifecycle() = delete;

#if defined(_WIN32)
    [[nodiscard]] static bool Initialize(EditorApplicationState& state);
    static void Shutdown(EditorApplicationState& state);
#endif
};

} // namespace kb::editor
