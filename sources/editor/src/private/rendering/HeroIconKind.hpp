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
    LockClosed,
    Server,
    WrenchScrewdriver,
    CodeBracket,
    RocketLaunch,
    Save,
    PlatformWindows,
    PlatformAndroid,
    PlatformLinux,

    // Sentinel for anything sized by the catalogue. Adding a kind above this line must
    // never require a second edit somewhere else to keep a cache in range.
    Count,
};

} // namespace kb::editor
