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
// Fluent UI System Icons `ic_fluent_code_24_filled` (MIT). The Heroicons code-bracket
// this replaces lost its angle brackets at row height and read as a bare slash.
static constexpr std::array<HeroIconPath, 1> kCodeBracket{
    HeroIconPath{ "M8.08553 18.6112L14.0817 4.60666C14.299 4.09895 14.8868 3.8636 15.3946 4.08097C15.866 4.28283 16.1026 4.80407 15.96 5.284L15.9202 5.39385L9.92409 19.3984C9.70671 19.9061 9.11892 20.1414 8.61121 19.9241C8.13977 19.7222 7.90316 19.201 8.04581 18.721L8.08553 18.6112L14.0817 4.60666L8.08553 18.6112ZM2.29289 11.2931L6.29289 7.29315C6.68342 6.90263 7.31658 6.90263 7.70711 7.29315C8.06759 7.65363 8.09532 8.22087 7.7903 8.61316L7.70711 8.70736L4.41421 12.0003L7.70711 15.2931C8.09763 15.6837 8.09763 16.3168 7.70711 16.7074C7.34662 17.0678 6.77939 17.0956 6.3871 16.7906L6.29289 16.7074L2.29289 12.7074C1.93241 12.3469 1.90468 11.7796 2.2097 11.3874L2.29289 11.2931L6.29289 7.29315L2.29289 11.2931ZM16.2921 7.29191C16.6526 6.93144 17.2198 6.90374 17.6121 7.20878L17.7063 7.29198L21.7071 11.2932C22.0678 11.6538 22.0953 12.2214 21.7899 12.6136L21.7066 12.7078L17.7058 16.7034C17.315 17.0936 16.6818 17.0932 16.2916 16.7024C15.9313 16.3417 15.904 15.7744 16.2093 15.3824L16.2925 15.2882L19.5854 11.9998L16.292 8.70613C15.9015 8.31558 15.9015 7.68242 16.2921 7.29191Z", true },
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
// Platform marks and the dedicated-server glyph, vendored rather than drawn. Brand marks
// name a build target here, which is what they are for. Sources and licences are recorded
// in third_party/devicon and third_party/fluentui-system-icons.
constexpr COLORREF kWindowsBlue = RGB(0, 173, 239);
constexpr COLORREF kAndroidGreen = RGB(61, 220, 132);
// Tux is black-on-white artwork. The body is lifted off black so it separates from a dark
// panel; the belly, beak and feet keep the colours the mark is known by.
constexpr COLORREF kTuxBody = RGB(41, 47, 59);
constexpr COLORREF kTuxBelly = RGB(245, 248, 250);
constexpr COLORREF kTuxBeak = RGB(255, 172, 51);
constexpr COLORREF kServerTone = RGB(147, 165, 194);

// devicon `icons/windows8/windows8-original.svg` (MIT), brand colour kept.
static constexpr std::array<HeroIconPath, 1> kPlatformWindows{
    HeroIconPath{ "M126 1.637l-67 9.834v49.831l67-.534zM1.647 66.709l.003 42.404 50.791 6.983-.04-49.057zm56.82.68l.094 49.465 67.376 9.509.016-58.863zM1.61 19.297l.047 42.383 50.791-.289-.023-49.016z", true, false, kWindowsBlue },
};
// devicon `icons/android/android-plain.svg` (MIT), drawn in the current brand green.
static constexpr std::array<HeroIconPath, 1> kPlatformAndroid{
    HeroIconPath{ "M21.005 43.003c-4.053-.002-7.338 3.291-7.339 7.341l.005 30.736a7.338 7.338 0 007.342 7.343 7.33 7.33 0 007.338-7.342V50.34a7.345 7.345 0 00-7.346-7.337m59.193-27.602l5.123-9.355a1.023 1.023 0 00-.401-1.388 1.022 1.022 0 00-1.382.407l-5.175 9.453c-4.354-1.938-9.227-3.024-14.383-3.019-5.142-.005-10.013 1.078-14.349 3.005L44.45 5.075a1.01 1.01 0 00-1.378-.406 1.007 1.007 0 00-.404 1.38l5.125 9.349c-10.07 5.193-16.874 15.083-16.868 26.438l66.118-.008c.002-11.351-6.79-21.221-16.845-26.427M48.942 29.858a2.772 2.772 0 01.003-5.545 2.78 2.78 0 012.775 2.774 2.776 2.776 0 01-2.778 2.771m30.106-.005a2.77 2.77 0 01-2.772-2.771 2.793 2.793 0 012.773-2.778 2.79 2.79 0 012.767 2.779 2.767 2.767 0 01-2.768 2.77M31.195 44.39l.011 47.635a7.822 7.822 0 007.832 7.831l5.333.002.006 16.264c-.001 4.05 3.291 7.342 7.335 7.342 4.056 0 7.342-3.295 7.343-7.347l-.004-16.26 9.909-.003.004 16.263c0 4.047 3.293 7.346 7.338 7.338 4.056.003 7.344-3.292 7.343-7.344l-.005-16.259 5.352-.004a7.835 7.835 0 007.836-7.834l-.009-47.635-65.624.011zm83.134 5.943a7.338 7.338 0 00-7.341-7.339c-4.053-.004-7.337 3.287-7.337 7.342l.006 30.738a7.334 7.334 0 007.339 7.339 7.337 7.337 0 007.338-7.343l-.005-30.737z", true, false, kAndroidGreen },
};
// devicon `icons/linux/linux-plain.svg` (MIT) is the official Tux silhouette and one
// tone. The coloured Tux upstream is a gradient illustration this renderer cannot take, so
// the flat regions below are painted over the vendored shape: belly, beak, feet and eyes.
// Those overlays are ours; the outline underneath is the real mark, and every one of them
// is placed in its 128 viewBox so they move with it.
static constexpr std::array<HeroIconPath, 6> kPlatformLinux{
    HeroIconPath{ "M113.823 104.595c-1.795-1.478-3.629-2.921-5.308-4.525-1.87-1.785-3.045-3.944-2.789-6.678.147-1.573-.216-2.926-2.113-3.452.446-1.154.864-1.928 1.033-2.753.188-.92.178-1.887.204-2.834.264-9.96-3.334-18.691-8.663-26.835-2.454-3.748-5.017-7.429-7.633-11.066-4.092-5.688-5.559-12.078-5.633-18.981a47.564 47.564 0 00-1.081-9.475C80.527 11.956 77.291 7.233 71.422 4.7c-4.497-1.942-9.152-2.327-13.901-1.084-6.901 1.805-11.074 6.934-10.996 14.088.074 6.885.417 13.779.922 20.648.288 3.893-.312 7.252-2.895 10.34-2.484 2.969-4.706 6.172-6.858 9.397-1.229 1.844-2.317 3.853-3.077 5.931-2.07 5.663-3.973 11.373-7.276 16.5-1.224 1.9-1.363 4.026-.494 6.199.225.563.363 1.429.089 1.882-2.354 3.907-5.011 7.345-10.066 8.095-3.976.591-4.172 1.314-4.051 5.413.1 3.337.061 6.705-.28 10.021-.363 3.555.008 4.521 3.442 5.373 7.924 1.968 15.913 3.647 23.492 6.854 3.227 1.365 6.465.891 9.064-1.763 2.713-2.771 6.141-3.855 9.844-3.859 6.285-.005 12.572.298 18.86.369 1.702.02 2.679.653 3.364 2.199.84 1.893 2.26 3.284 4.445 3.526 4.193.462 8.013-.16 11.19-3.359 3.918-3.948 8.436-7.066 13.615-9.227 1.482-.619 2.878-1.592 4.103-2.648 2.231-1.922 2.113-3.146-.135-5zM62.426 24.12c.758-2.601 2.537-4.289 5.243-4.801 2.276-.43 4.203.688 5.639 3.246 1.546 2.758 2.054 5.64.734 8.658-1.083 2.474-1.591 2.707-4.123 1.868-.474-.157-.937-.343-1.777-.652.708-.594 1.154-1.035 1.664-1.382 1.134-.772 1.452-1.858 1.346-3.148-.139-1.694-1.471-3.194-2.837-3.175-1.225.017-2.262 1.167-2.4 2.915-.086 1.089.095 2.199.173 3.589-3.446-1.023-4.711-3.525-3.662-7.118zm-12.75-2.251c1.274-1.928 3.197-2.314 5.101-1.024 2.029 1.376 3.547 5.256 2.763 7.576-.285.844-1.127 1.5-1.716 2.241l-.604-.374c-.23-1.253-.276-2.585-.757-3.733-.304-.728-1.257-1.184-1.919-1.762-.622.739-1.693 1.443-1.757 2.228-.088 1.084.477 2.28.969 3.331.311.661 1.001 1.145 1.713 1.916l-1.922 1.51c-3.018-2.7-3.915-8.82-1.871-11.909zM87.34 86.075c-.203 2.604-.5 2.713-3.118 3.098-1.859.272-2.359.756-2.453 2.964a101.744 101.744 0 00-.012 7.753c.061 1.77-.537 3.158-1.755 4.393-6.764 6.856-14.845 10.105-24.512 8.926-4.17-.509-6.896-3.047-9.097-6.639.98-.363 1.705-.607 2.412-.894 3.122-1.27 3.706-3.955 1.213-6.277-1.884-1.757-3.986-3.283-6.007-4.892-1.954-1.555-3.934-3.078-5.891-4.629-1.668-1.323-2.305-3.028-2.345-5.188-.094-5.182.972-10.03 3.138-14.747 1.932-4.209 3.429-8.617 5.239-12.885.935-2.202 1.906-4.455 3.278-6.388 1.319-1.854 2.134-3.669 1.988-5.94-.084-1.276-.016-2.562-.016-3.843l.707-.352c1.141.985 2.302 1.949 3.423 2.959 4.045 3.646 7.892 3.813 12.319.67 1.888-1.341 3.93-2.47 5.927-3.652.497-.294 1.092-.423 1.934-.738 2.151 5.066 4.262 10.033 6.375 15 1.072 2.524 1.932 5.167 3.264 7.547 2.671 4.775 4.092 9.813 4.07 15.272-.012 2.83.137 5.67-.081 8.482z", true, true, kTuxBody },
    // Belly: an ellipse inside the lower body, leaving the silhouette as its outline.
    HeroIconPath{ "M89 90c0 14.9-11.2 27-25 27-13.8 0-25-12.1-25-27 0-14.9 11.2-27 25-27 13.8 0 25 12.1 25 27Z", true, false, kTuxBelly },
    // Beak, between and just under the eyes.
    HeroIconPath{ "M64 31c7 0 12 3.2 12 6.5 0 3.3-5 6.5-12 6.5-7 0-12-3.2-12-6.5 0-3.3 5-6.5 12-6.5Z", true, false, kTuxBeak },
    // Feet, over the two the silhouette already carries.
    HeroIconPath{ "M48 110c-6 5-14 9-20 10-5 .9-6.5-2.6-2.5-5.2l16.5-9.8 6 5Zm32 0c6 5 14 9 20 10 5 .9 6.5-2.6 2.5-5.2l-16.5-9.8-6 5Z", true, false, kTuxBeak },
    // The silhouette carves its eyes out as holes, which on a dark panel reads as nothing.
    HeroIconPath{ "M55 20a5.5 5.5 0 1 1 0 11 5.5 5.5 0 1 1 0-11Zm18 0a5.5 5.5 0 1 1 0 11 5.5 5.5 0 1 1 0-11Z", true, false, kTuxBelly },
    HeroIconPath{ "M55 23a2.4 2.4 0 1 1 0 4.8 2.4 2.4 0 1 1 0-4.8Zm18 0a2.4 2.4 0 1 1 0 4.8 2.4 2.4 0 1 1 0-4.8Z", true, false, kTuxBody },
};
// Fluent UI System Icons `ic_fluent_server_24_filled` (MIT).
static constexpr std::array<HeroIconPath, 1> kPlatformServer{
    HeroIconPath{ "M9 2C7.34315 2 6 3.34315 6 5V19C6 20.6569 7.34315 22 9 22H15C16.6569 22 18 20.6569 18 19V5C18 3.34315 16.6569 2 15 2H9ZM8.5 6.75C8.5 6.33579 8.83579 6 9.25 6H14.75C15.1642 6 15.5 6.33579 15.5 6.75C15.5 7.16421 15.1642 7.5 14.75 7.5H9.25C8.83579 7.5 8.5 7.16421 8.5 6.75ZM8.5 17.75C8.5 17.3358 8.83579 17 9.25 17H14.75C15.1642 17 15.5 17.3358 15.5 17.75C15.5 18.1642 15.1642 18.5 14.75 18.5H9.25C8.83579 18.5 8.5 18.1642 8.5 17.75ZM8.5 14.75C8.5 14.3358 8.83579 14 9.25 14H14.75C15.1642 14 15.5 14.3358 15.5 14.75C15.5 15.1642 15.1642 15.5 14.75 15.5H9.25C8.83579 15.5 8.5 15.1642 8.5 14.75Z", true, false, kServerTone },
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
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformWindows }, .viewBoxSize = 128.0F };
}

HeroIconGlyph HeroIconAssets::PlatformAndroid() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformAndroid }, .viewBoxSize = 128.0F };
}

HeroIconGlyph HeroIconAssets::PlatformLinux() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlatformLinux }, .viewBoxSize = 128.0F };
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
