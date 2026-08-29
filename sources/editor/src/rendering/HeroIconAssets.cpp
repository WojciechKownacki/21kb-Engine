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

// Heroicons solid "lock-closed" (MIT).
static constexpr std::array<HeroIconPath, 1> kLockClosed{
    HeroIconPath{ "M12 1.5a5.25 5.25 0 0 0-5.25 5.25v3a3 3 0 0 0-3 3v6.75a3 3 0 0 0 3 3h10.5a3 3 0 0 0 3-3v-6.75a3 3 0 0 0-3-3v-3c0-2.9-2.35-5.25-5.25-5.25Zm3.75 8.25v-3a3.75 3.75 0 1 0-7.5 0v3h7.5Z", true },
};
// Heroicons solid "server" (MIT).
static constexpr std::array<HeroIconPath, 2> kServer{
    HeroIconPath{ "M4.08 5.227A3 3 0 0 1 6.979 3H17.02a3 3 0 0 1 2.9 2.227l2.113 7.926A5.228 5.228 0 0 0 18.75 12H5.25a5.228 5.228 0 0 0-3.284 1.153L4.08 5.227Z", true },
    HeroIconPath{ "M5.25 13.5a3.75 3.75 0 1 0 0 7.5h13.5a3.75 3.75 0 1 0 0-7.5H5.25Zm10.5 4.5a.75.75 0 1 0 0-1.5.75.75 0 0 0 0 1.5Zm3.75-.75a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
};
// Heroicons solid "wrench-screwdriver" (MIT).
static constexpr std::array<HeroIconPath, 3> kWrenchScrewdriver{
    HeroIconPath{ "M12 6.75a5.25 5.25 0 0 1 6.775-5.025.75.75 0 0 1 .313 1.248l-3.32 3.319c.063.475.276.934.641 1.299.365.365.824.578 1.3.64l3.318-3.319a.75.75 0 0 1 1.248.313 5.25 5.25 0 0 1-5.472 6.756c-1.018-.086-1.87.1-2.309.634L7.344 21.3A3.298 3.298 0 1 1 2.7 16.657l8.684-7.151c.533-.44.72-1.291.634-2.309A5.342 5.342 0 0 1 12 6.75ZM4.117 19.125a.75.75 0 0 1 .75-.75h.008a.75.75 0 0 1 .75.75v.008a.75.75 0 0 1-.75.75h-.008a.75.75 0 0 1-.75-.75v-.008Z", true },
    HeroIconPath{ "M10.076 8.64 7.875 6.44V4.874a.75.75 0 0 0-.364-.643l-3.75-2.25a.75.75 0 0 0-.916.113l-.75.75a.75.75 0 0 0-.113.916l2.25 3.75a.75.75 0 0 0 .643.364h1.564l2.062 2.062 1.575-1.297Z", true },
    HeroIconPath{ "M12.556 17.329 16.739 21.511a3.375 3.375 0 0 0 4.773-4.773l-3.306-3.305a6.803 6.803 0 0 1-1.53.043c-.394-.034-.682-.006-.867.042a.589.589 0 0 0-.167.063l-3.086 3.748Zm3.414-1.36a.75.75 0 0 1 1.06 0l1.875 1.876a.75.75 0 1 1-1.06 1.06L15.97 17.03a.75.75 0 0 1 0-1.06Z", true },
};
// Heroicons solid "code-bracket" (MIT).
static constexpr std::array<HeroIconPath, 1> kCodeBracket{
    HeroIconPath{ "M14.447 3.026a.75.75 0 0 1 .527.921l-4.5 16.5a.75.75 0 0 1-1.448-.394l4.5-16.5a.75.75 0 0 1 .921-.527ZM16.72 6.22a.75.75 0 0 1 1.06 0l5.25 5.25a.75.75 0 0 1 0 1.06l-5.25 5.25a.75.75 0 1 1-1.06-1.06L21.44 12l-4.72-4.72a.75.75 0 0 1 0-1.06Zm-9.44 0a.75.75 0 0 1 0 1.06L2.56 12l4.72 4.72a.75.75 0 0 1-1.06 1.06L.97 12.53a.75.75 0 0 1 0-1.06l5.25-5.25a.75.75 0 0 1 1.06 0Z", true },
};
// Heroicons solid "rocket-launch" (MIT).
static constexpr std::array<HeroIconPath, 2> kRocketLaunch{
    HeroIconPath{ "M9.315 7.584C12.195 3.883 16.695 1.5 21.75 1.5a.75.75 0 0 1 .75.75c0 5.056-2.383 9.555-6.084 12.436A6.75 6.75 0 0 1 9.75 22.5a.75.75 0 0 1-.75-.75v-4.131A15.838 15.838 0 0 1 6.382 15H2.25a.75.75 0 0 1-.75-.75 6.75 6.75 0 0 1 7.815-6.666ZM15 6.75a2.25 2.25 0 1 0 0 4.5 2.25 2.25 0 0 0 0-4.5Z", true },
    HeroIconPath{ "M5.26 17.242a.75.75 0 1 0-.897-1.203 5.243 5.243 0 0 0-2.05 5.022.75.75 0 0 0 .625.627 5.243 5.243 0 0 0 5.022-2.051.75.75 0 1 0-1.202-.897 3.744 3.744 0 0 1-3.008 1.51c0-1.23.592-2.323 1.51-3.008Z", true },
};
// Lucide "save" (ISC).
static constexpr std::array<HeroIconPath, 3> kSave{
    HeroIconPath{ "M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z", false },
    HeroIconPath{ "M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7", false },
    HeroIconPath{ "M7 3v4a1 1 0 0 0 1 1h7", false },
};
// Platform marks, drawn here in the brand colours rather than vendored. No permissively
// licensed icon set carries these - Simple Icons dropped the Microsoft ones over trademark
// policy and ships the rest as single-colour silhouettes - and the official artwork comes
// under brand guidelines, not a licence we could audit into third_party. So these are our
// shapes wearing the official colours: recognisable at row height, and honestly ours.
constexpr COLORREF kWindowsBlue = RGB(0, 120, 212);
constexpr COLORREF kAndroidGreen = RGB(61, 220, 132);
// Tux is black on white artwork. A black body disappears into a dark panel, so the body
// carries the lightest shade that still reads as "dark bird" against it; the belly, beak
// and feet keep their real colours, which is what actually makes the mark readable.
constexpr COLORREF kTuxBody = RGB(63, 70, 84);
constexpr COLORREF kTuxBelly = RGB(244, 246, 250);
constexpr COLORREF kTuxBeak = RGB(247, 181, 41);
constexpr COLORREF kServerChassis = RGB(104, 122, 152);
constexpr COLORREF kServerFace = RGB(147, 165, 194);
constexpr COLORREF kServerLed = RGB(74, 222, 128);

static constexpr std::array<HeroIconPath, 4> kPlatformWindows{
    HeroIconPath{ "M3 3.75h8.25V12H3V3.75Z", true, true, kWindowsBlue },
    HeroIconPath{ "M12.75 3.75H21V12h-8.25V3.75Z", true, true, kWindowsBlue },
    HeroIconPath{ "M3 13.5h8.25v6.75H3V13.5Z", true, true, kWindowsBlue },
    HeroIconPath{ "M12.75 13.5H21v6.75h-8.25V13.5Z", true, true, kWindowsBlue },
};
// The dome and its two eyes are one path so the even-odd rule punches the eyes out of it.
static constexpr std::array<HeroIconPath, 4> kPlatformAndroid{
    HeroIconPath{ "M7.1 3.1 8.6 5.6M16.9 3.1 15.4 5.6", false, true, kAndroidGreen },
    HeroIconPath{ "M4 11.4a8 8 0 0 1 16 0H4Zm5.1-2.6a.95.95 0 1 0 0-1.9.95.95 0 0 0 0 1.9Zm5.8 0a.95.95 0 1 0 0-1.9.95.95 0 0 0 0 1.9Z", true, true, kAndroidGreen },
    HeroIconPath{ "M4.6 12.8h14.8v6.4a2 2 0 0 1-2 2H6.6a2 2 0 0 1-2-2v-6.4Z", true, true, kAndroidGreen },
    HeroIconPath{ "M1.15 14.15a1.35 1.35 0 0 1 2.7 0v4.1a1.35 1.35 0 0 1-2.7 0v-4.1Zm19 0a1.35 1.35 0 0 1 2.7 0v4.1a1.35 1.35 0 0 1-2.7 0v-4.1Z", true, true, kAndroidGreen },
};
// Body, then the white belly and face over it, then the pupils, beak and feet.
static constexpr std::array<HeroIconPath, 6> kPlatformLinux{
    HeroIconPath{ "M12 2.2c-2.6 0-4.4 2-4.4 4.6v2.1c0 1-.4 1.7-1 2.5C5.3 13 4.6 15 4.6 16.9c0 2.6 1.9 4.6 4.3 4.6h6.2c2.4 0 4.3-2 4.3-4.6 0-1.9-.7-3.9-2-5.5-.6-.8-1-1.5-1-2.5V6.8c0-2.6-1.8-4.6-4.4-4.6Z", true, true, kTuxBody },
    HeroIconPath{ "M12 11.2c2.6 0 4.4 2.4 4.4 5.2 0 2.4-1.9 4.1-4.4 4.1-2.5 0-4.4-1.7-4.4-4.1 0-2.8 1.8-5.2 4.4-5.2Z", true, true, kTuxBelly },
    HeroIconPath{ "M10.1 5.2a1.5 1.5 0 1 1 0 3 1.5 1.5 0 0 1 0-3Zm3.8 0a1.5 1.5 0 1 1 0 3 1.5 1.5 0 0 1 0-3Z", true, true, kTuxBelly },
    HeroIconPath{ "M10.2 6a.75.75 0 1 1 0 1.5.75.75 0 0 1 0-1.5Zm3.6 0a.75.75 0 1 1 0 1.5.75.75 0 0 1 0-1.5Z", true, true, kTuxBody },
    HeroIconPath{ "M10.4 8.6h3.2L12 10.5 10.4 8.6Z", true, true, kTuxBeak },
    HeroIconPath{ "M7.4 20.6c-.9.5-2 .8-2.9.6-.7-.2-.8-.9-.2-1.3l2.4-1.5 .7 2.2Zm9.2 0c.9.5 2 .8 2.9.6.7-.2.8-.9.2-1.3l-2.4-1.5-.7 2.2Z", true, true, kTuxBeak },
};
// Original editor glyph: a two-unit rack. There is no brand mark for a dedicated server,
// so this is a plain object drawn to sit beside the platform marks without competing.
static constexpr std::array<HeroIconPath, 5> kPlatformServer{
    HeroIconPath{ "M3.6 4h16.8A1.6 1.6 0 0 1 22 5.6v3.8a1.6 1.6 0 0 1-1.6 1.6H3.6A1.6 1.6 0 0 1 2 9.4V5.6A1.6 1.6 0 0 1 3.6 4Z", true, true, kServerChassis },
    HeroIconPath{ "M3.6 13h16.8a1.6 1.6 0 0 1 1.6 1.6v3.8a1.6 1.6 0 0 1-1.6 1.6H3.6A1.6 1.6 0 0 1 2 18.4v-3.8A1.6 1.6 0 0 1 3.6 13Z", true, true, kServerChassis },
    HeroIconPath{ "M4.8 6.8h7.4v1.4H4.8V6.8Z", true, true, kServerFace },
    HeroIconPath{ "M4.8 15.8h7.4v1.4H4.8v-1.4Z", true, true, kServerFace },
    HeroIconPath{ "M18.4 6.35a1.15 1.15 0 1 1 0 2.3 1.15 1.15 0 0 1 0-2.3Zm0 9a1.15 1.15 0 1 1 0 2.3 1.15 1.15 0 0 1 0-2.3Z", true, true, kServerLed },
};

// Original editor glyph: the disclosure caret. Heroicons and Lucide both carry chevrons
// rather than a filled triangle, and the panels want the triangle - so it is drawn here,
// but drawn through the icon painter, which antialiases. The GDI Polygon it replaces did
// not, which is the whole of why the old arrow looked ragged.
static constexpr std::array<HeroIconPath, 1> kDisclosureCollapsed{
    HeroIconPath{ "M9 5.5 17 12l-8 6.5V5.5Z", true },
};
static constexpr std::array<HeroIconPath, 1> kDisclosureExpanded{
    HeroIconPath{ "M5.5 9H18.5L12 17 5.5 9Z", true },
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

HeroIconGlyph HeroIconAssets::LockClosed() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kLockClosed } };
}

HeroIconGlyph HeroIconAssets::Server() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kServer } };
}

HeroIconGlyph HeroIconAssets::WrenchScrewdriver() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kWrenchScrewdriver } };
}

HeroIconGlyph HeroIconAssets::CodeBracket() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCodeBracket } };
}

HeroIconGlyph HeroIconAssets::RocketLaunch() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kRocketLaunch } };
}

HeroIconGlyph HeroIconAssets::Save() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kSave }, .strokeWidth = 2.0F };
}

HeroIconGlyph HeroIconAssets::PlatformWindows() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformWindows } };
}

HeroIconGlyph HeroIconAssets::PlatformAndroid() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformAndroid }, .strokeWidth = 1.7F };
}

HeroIconGlyph HeroIconAssets::PlatformLinux() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformLinux } };
}

HeroIconGlyph HeroIconAssets::PlatformServer() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformServer } };
}

HeroIconGlyph HeroIconAssets::DisclosureCollapsed() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kDisclosureCollapsed } };
}

HeroIconGlyph HeroIconAssets::DisclosureExpanded() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kDisclosureExpanded } };
}

} // namespace kb::editor
