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
    Check,
    Cube,
    Folder,
    Eye,
    MagnifyingGlass,
    ChevronRight,
    ChevronLeft,
    ChevronDown,
    SpeakerWave,
    Plus,
    EllipsisHorizontal,
    ListBullet,
    AdjustmentsHorizontal,
    CommandLine,
    DocumentText,
    Bolt,
    RectangleGroup,
    Gamepad2,
    RotationSnap,
    Camera,
    Skeleton,
};

} // namespace kb::editor
