#pragma once

#include <cstdint>

namespace kb::editor {

enum class FloatingWindowControlKind : std::uint8_t {
    None,
    Minimize,
    MaximizeRestore,
    Close,
};

} // namespace kb::editor
