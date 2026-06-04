#pragma once

#include "app/EditorApplicationState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorApplicationMessageLoop {
public:
    EditorApplicationMessageLoop() = delete;

#if defined(_WIN32)
    static void Run(EditorApplicationState& state);
#endif
};

} // namespace kb::editor
