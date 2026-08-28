#include "rendering/HeroIconAssets.hpp"

#include <array>

namespace kb::editor {
namespace {

static constexpr std::array<HeroIconPath, 1> kMinus{
    HeroIconPath{ "M5 12h14", false },
};
// Source: LuizEngine `icons::lucide::kPlay` / lucide.dev play, ISC.
static constexpr std::array<HeroIconPath, 1> kPlay{
    HeroIconPath{ "M6 3L20 12L6 21V3Z", true },
};
// Source: LuizEngine `icons::lucide::kPause` / lucide.dev pause, ISC.
static constexpr std::array<HeroIconPath, 2> kPause{
    HeroIconPath{ "M6 4H10V20H6V4Z", true },
    HeroIconPath{ "M14 4H18V20H14V4Z", true },
};
// Source: LuizEngine `icons::lucide::kResume` / lucide.dev step-forward, ISC.
static constexpr std::array<HeroIconPath, 2> kResume{
    HeroIconPath{ "M5 4L15 12L5 20V4Z", true },
    HeroIconPath{ "M19 5V19", false },
};
static constexpr std::array<HeroIconPath, 1> kStop{
    HeroIconPath{ "M5.25 7.5A2.25 2.25 0 0 1 7.5 5.25h9a2.25 2.25 0 0 1 2.25 2.25v9a2.25 2.25 0 0 1-2.25 2.25h-9a2.25 2.25 0 0 1-2.25-2.25v-9Z", false },
};
// Source: LuizEngine `icons::lucide::kStop` / lucide.dev square, ISC.
static constexpr std::array<HeroIconPath, 1> kTransportStop{
    HeroIconPath{ "M7 5H17A2 2 0 0 1 19 7V17A2 2 0 0 1 17 19H7A2 2 0 0 1 5 17V7A2 2 0 0 1 7 5Z", true },
};
static constexpr std::array<HeroIconPath, 1> kXMark{
    HeroIconPath{ "M6 18 18 6M6 6l12 12", false },
};
static constexpr std::array<HeroIconPath, 1> kCheck{
    HeroIconPath{ "M4.5 12.75l6 6 9-13.5", false },
};
static constexpr std::array<HeroIconPath, 3> kCube{
    HeroIconPath{ "M12.3779 1.60217C12.1444 1.46594 11.8556 1.46594 11.6221 1.60217L3 6.63172L12 11.8817L21 6.63172L12.3779 1.60217Z", true },
    HeroIconPath{ "M21.75 7.93078L12.75 13.1808V22.1808L21.3779 17.1478C21.6083 17.0134 21.75 16.7668 21.75 16.5V7.93078Z", true },
    HeroIconPath{ "M11.25 22.1808V13.1808L2.25 7.93078V16.5C2.25 16.7668 2.39168 17.0134 2.6221 17.1478L11.25 22.1808Z", true },
};
static constexpr std::array<HeroIconPath, 1> kFolder{
    HeroIconPath{ "M19.5 21a3 3 0 0 0 3-3v-4.5a3 3 0 0 0-3-3h-15a3 3 0 0 0-3 3V18a3 3 0 0 0 3 3h15ZM1.5 10.146V6a3 3 0 0 1 3-3h5.379a2.25 2.25 0 0 1 1.59.659l2.122 2.121c.14.141.331.22.53.22H19.5a3 3 0 0 1 3 3v1.146A4.483 4.483 0 0 0 19.5 9h-15a4.483 4.483 0 0 0-3 1.146Z", true },
};
static constexpr std::array<HeroIconPath, 2> kEye{
    HeroIconPath{ "M8 9.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Z", true },
    HeroIconPath{ "M1.38 8.28a.87.87 0 0 1 0-.566 7.003 7.003 0 0 1 13.244.005.87.87 0 0 1 0 .566A7.003 7.003 0 0 1 1.379 8.28ZM11 8a3 3 0 1 1-6 0 3 3 0 0 1 6 0Z", true },
};
static constexpr std::array<HeroIconPath, 1> kMagnifyingGlass{
    HeroIconPath{ "m21 21-5.197-5.197m0 0A7.5 7.5 0 1 0 5.196 5.196a7.5 7.5 0 0 0 10.607 10.607Z", false },
};
static constexpr std::array<HeroIconPath, 1> kChevronRight{
    HeroIconPath{ "m8.25 4.5 7.5 7.5-7.5 7.5", false },
};
// Horizontal mirror of kChevronRight — the "back" arrow, same painted style as
// the category chevron.
static constexpr std::array<HeroIconPath, 1> kChevronLeft{
    HeroIconPath{ "m15.75 4.5-7.5 7.5 7.5 7.5", false },
};
static constexpr std::array<HeroIconPath, 1> kChevronDown{
    HeroIconPath{ "m19.5 8.25-7.5 7.5-7.5-7.5", false },
};
// Heroicons outline "speaker-wave" (MIT). Arc flags spaced for the SVG parser.
static constexpr std::array<HeroIconPath, 1> kSpeakerWave{
    HeroIconPath{ "M19.114 5.636a9 9 0 0 1 0 12.728M16.463 8.288a5.25 5.25 0 0 1 0 7.424M6.75 8.25l4.72-4.72a.75.75 0 0 1 1.28.53v15.88a.75.75 0 0 1-1.28.53l-4.72-4.72H4.51c-.88 0-1.704-.507-1.938-1.354A9.009 9.009 0 0 1 2.25 12c0-.83.112-1.633.322-2.396C2.806 8.756 3.63 8.25 4.51 8.25H6.75Z", false },
};
static constexpr std::array<HeroIconPath, 1> kPlus{
    HeroIconPath{ "M12 4.5v15m7.5-7.5h-15", false },
};
static constexpr std::array<HeroIconPath, 3> kEllipsisHorizontal{
    HeroIconPath{ "M6.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
    HeroIconPath{ "M12.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
    HeroIconPath{ "M18.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
};
static constexpr std::array<HeroIconPath, 1> kListBullet{
    HeroIconPath{ "M3 4.75a1 1 0 1 0 0-2 1 1 0 0 0 0 2ZM6.25 3a.75.75 0 0 0 0 1.5h7a.75.75 0 0 0 0-1.5h-7ZM6.25 7.25a.75.75 0 0 0 0 1.5h7a.75.75 0 0 0 0-1.5h-7ZM6.25 11.5a.75.75 0 0 0 0 1.5h7a.75.75 0 0 0 0-1.5h-7ZM4 12.25a1 1 0 1 1-2 0 1 1 0 0 1 2 0ZM3 9a1 1 0 1 0 0-2 1 1 0 0 0 0 2Z", true },
};
static constexpr std::array<HeroIconPath, 1> kAdjustmentsHorizontal{
    HeroIconPath{ "M6.5 2.25a.75.75 0 0 0-1.5 0v3a.75.75 0 0 0 1.5 0V4.5h6.75a.75.75 0 0 0 0-1.5H6.5v-.75ZM11 6.5a.75.75 0 0 0-1.5 0v3a.75.75 0 0 0 1.5 0v-.75h2.25a.75.75 0 0 0 0-1.5H11V6.5ZM5.75 10a.75.75 0 0 1 .75.75v.75h6.75a.75.75 0 0 1 0 1.5H6.5v.75a.75.75 0 0 1-1.5 0v-3a.75.75 0 0 1 .75-.75ZM2.75 7.25H8.5v1.5H2.75a.75.75 0 0 1 0-1.5ZM4 3H2.75a.75.75 0 0 0 0 1.5H4V3ZM2.75 11.5H4V13H2.75a.75.75 0 0 1 0-1.5Z", true },
};
static constexpr std::array<HeroIconPath, 1> kCommandLine{
    HeroIconPath{ "M2 4a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V4Zm2.22 1.97a.75.75 0 0 0 0 1.06l.97.97-.97.97a.75.75 0 1 0 1.06 1.06l1.5-1.5a.75.75 0 0 0 0-1.06l-1.5-1.5a.75.75 0 0 0-1.06 0ZM8.75 8.5a.75.75 0 0 0 0 1.5h2.5a.75.75 0 0 0 0-1.5h-2.5Z", true },
};
static constexpr std::array<HeroIconPath, 1> kDocumentText{
    HeroIconPath{ "M19.5 14.25v-2.625a3.375 3.375 0 0 0-3.375-3.375h-1.5A1.125 1.125 0 0 1 13.5 7.125v-1.5a3.375 3.375 0 0 0-3.375-3.375H8.25m0 12.75h7.5m-7.5 3H12M10.5 2.25H5.625c-.621 0-1.125.504-1.125 1.125v17.25c0 .621.504 1.125 1.125 1.125h12.75c.621 0 1.125-.504 1.125-1.125V11.25a9 9 0 0 0-9-9Z", false },
};
static constexpr std::array<HeroIconPath, 1> kBolt{
    HeroIconPath{ "m3.75 13.5 10.5-11.25L12 10.5h8.25L9.75 21.75 12 13.5H3.75Z", false },
};
static constexpr std::array<HeroIconPath, 3> kRectangleGroup{
    HeroIconPath{ "M2.25 7.125C2.25 6.504 2.754 6 3.375 6h6c.621 0 1.125.504 1.125 1.125v3.75c0 .621-.504 1.125-1.125 1.125h-6a1.125 1.125 0 0 1-1.125-1.125v-3.75Z", false },
    HeroIconPath{ "M14.25 8.625c0-.621.504-1.125 1.125-1.125h5.25c.621 0 1.125.504 1.125 1.125v8.25c0 .621-.504 1.125-1.125 1.125h-5.25a1.125 1.125 0 0 1-1.125-1.125v-8.25Z", false },
    HeroIconPath{ "M3.75 16.125c0-.621.504-1.125 1.125-1.125h5.25c.621 0 1.125.504 1.125 1.125v2.25c0 .621-.504 1.125-1.125 1.125h-5.25a1.125 1.125 0 0 1-1.125-1.125v-2.25Z", false },
};
// Source: LuizEngine `icons::lucide::kGamepad2` / lucide.dev gamepad-2, ISC.
static constexpr std::array<HeroIconPath, 5> kGamepad2{
    HeroIconPath{ "M6 11h4", false },
    HeroIconPath{ "M8 9v4", false },
    HeroIconPath{ "M15 12h.01", false },
    HeroIconPath{ "M18 10h.01", false },
    HeroIconPath{ "M17.32 5H6.68a4 4 0 0 0-3.98 3.59c-.01.05-.01.1-.02.15C2.6 9.42 2 14.46 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.41-1.41A2 2 0 0 1 9.83 16h4.34a2 2 0 0 1 1.42.59L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.54-.6-6.58-.68-7.26-.01-.05-.01-.1-.02-.15A4 4 0 0 0 17.32 5z", false },
};
// Custom editor icon: rotation snap / angle step.
static constexpr std::array<HeroIconPath, 3> kRotationSnap{
    HeroIconPath{ "M4.5 19.5H20M4.5 19.5V4", false },
    HeroIconPath{ "M7.5 19.5A12 12 0 0 1 19.5 7.5M10.5 19.5A9 9 0 0 1 19.5 10.5", false },
    HeroIconPath{ "M8.25 16.75l1.4-1.4M12.5 14.5v-2M16.25 12.25h-2M18.25 8.5l-1.55 1.55", false },
};
// Heroicons outline "video-camera" (MIT).
static constexpr std::array<HeroIconPath, 1> kCamera{
    HeroIconPath{ "M15.75 10.5 21 7.5v9l-5.25-3m-12-6.75h9a3 3 0 0 1 3 3v4.5a3 3 0 0 1-3 3h-9a3 3 0 0 1-3-3v-4.5a3 3 0 0 1 3-3Z", false },
};
// Original editor glyph: a compact humanoid joint hierarchy. Circles are
// joints and the connected strokes make the asset type readable at tile size.
static constexpr std::array<HeroIconPath, 2> kSkeleton{
    HeroIconPath{ "M12 3.25a1.75 1.75 0 1 1 0 3.5 1.75 1.75 0 0 1 0-3.5ZM7 9.25a1.25 1.25 0 1 1 0 2.5 1.25 1.25 0 0 1 0-2.5Zm10 0a1.25 1.25 0 1 1 0 2.5 1.25 1.25 0 0 1 0-2.5ZM9 18a1.25 1.25 0 1 1 0 2.5A1.25 1.25 0 0 1 9 18Zm6 0a1.25 1.25 0 1 1 0 2.5A1.25 1.25 0 0 1 15 18Z", false },
    HeroIconPath{ "M12 6.75v6.5m0-4.5-5 1.75m5-1.75 5 1.75m-5 2.75L9 18m3-4.75L15 18", false },
};

} // namespace

HeroIconGlyph HeroIconAssets::Minus() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kMinus } };
}

HeroIconGlyph HeroIconAssets::Play() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlay }, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::Pause() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPause }, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::Resume() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kResume }, .strokeWidth = 2.0F };
}

HeroIconGlyph HeroIconAssets::Stop() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kStop } };
}

HeroIconGlyph HeroIconAssets::TransportStop() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kTransportStop }, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::XMark() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kXMark } };
}

HeroIconGlyph HeroIconAssets::Check() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCheck } };
}

HeroIconGlyph HeroIconAssets::Cube() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCube } };
}

HeroIconGlyph HeroIconAssets::Folder() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kFolder }, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::Eye() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kEye }, .viewBoxSize = 16.0F, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::MagnifyingGlass() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kMagnifyingGlass } };
}

HeroIconGlyph HeroIconAssets::ChevronRight() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kChevronRight } };
}

HeroIconGlyph HeroIconAssets::ChevronLeft() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kChevronLeft } };
}

HeroIconGlyph HeroIconAssets::ChevronDown() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kChevronDown } };
}

HeroIconGlyph HeroIconAssets::SpeakerWave() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kSpeakerWave } };
}

HeroIconGlyph HeroIconAssets::Plus() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlus } };
}

HeroIconGlyph HeroIconAssets::EllipsisHorizontal() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kEllipsisHorizontal } };
}

HeroIconGlyph HeroIconAssets::ListBullet() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kListBullet }, .viewBoxSize = 16.0F, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::AdjustmentsHorizontal() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kAdjustmentsHorizontal }, .viewBoxSize = 16.0F, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::CommandLine() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCommandLine }, .viewBoxSize = 16.0F, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::DocumentText() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kDocumentText } };
}

HeroIconGlyph HeroIconAssets::Bolt() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kBolt } };
}

HeroIconGlyph HeroIconAssets::RectangleGroup() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kRectangleGroup } };
}

HeroIconGlyph HeroIconAssets::Gamepad2() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kGamepad2 }, .strokeWidth = 2.0F };
}

HeroIconGlyph HeroIconAssets::RotationSnap() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kRotationSnap }, .strokeWidth = 1.8F };
}

HeroIconGlyph HeroIconAssets::Camera() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCamera } };
}

HeroIconGlyph HeroIconAssets::Skeleton() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kSkeleton }, .strokeWidth = 1.8F };
}

} // namespace kb::editor
