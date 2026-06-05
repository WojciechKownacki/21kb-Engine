#pragma once

#include <cstdint>

namespace kb::editor {

enum class HeroIconKind : std::uint8_t {
    Minus,
    Play,
    Pause,
    Resume,
    Stop,
    TransportStop,
    XMark,
    Cube,
    Folder,
    Eye,
    MagnifyingGlass,
    ChevronRight,
    ChevronDown,
    Plus,
    EllipsisHorizontal,
    ListBullet,
    AdjustmentsHorizontal,
    CommandLine,
    Gamepad2,
};

} // namespace kb::editor
