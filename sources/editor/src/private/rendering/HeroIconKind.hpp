#pragma once

#include <cstdint>

namespace kb::editor {

enum class HeroIconKind : std::uint8_t {
    Minus,
    Stop,
    XMark,
    Cube,
    Eye,
    MagnifyingGlass,
    ChevronRight,
    ChevronDown,
    Plus,
    EllipsisHorizontal,
};

} // namespace kb::editor
