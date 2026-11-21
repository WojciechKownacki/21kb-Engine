#include "scene/EditorGameView.hpp"

#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kb::editor {
namespace {

struct Entry {
    EditorGameViewDevice device;
    std::string_view name;
    // The screen lying down.
    EditorGameViewSize landscape;
    // The bezel around the screen, as a part of the screen's short side.
    double bezel;
    // The metal rim around the bezel, in pixels of the panel.
    int rim;
};

constexpr Entry kEntries[] = {
    { EditorGameViewDevice::Computer, "Computer", { 1920U, 1080U }, 0.0, 1 },
    { EditorGameViewDevice::Tablet, "Tablet", { 1544U, 907U }, 0.065, 3 },
    { EditorGameViewDevice::Phone, "Phone", { 2340U, 1080U }, 0.05, 3 },
};

// The space kept free around the device in the panel.
constexpr int kMargin = 10;
// What a computer's two outlines leave free, as a part of the width and of the height: 90% and 80% remain.
constexpr double kOuterComputerMargin = 0.10;
constexpr double kInnerComputerMargin = 0.20;

[[nodiscard]] const Entry& EntryFor(EditorGameViewDevice device) noexcept {
    for (const Entry& entry : kEntries) {
        if (entry.device == device) {
            return entry;
        }
    }
    return kEntries[0];
}

[[nodiscard]] std::uint32_t Scaled(std::uint32_t value, double factor) noexcept {
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(value) * factor));
}

// The built-in profile that carries a device's insets; a tablet has one, which holds in both orientations.
[[nodiscard]] EditorViewportProfileKind ProfileKind(EditorGameViewDevice device, bool portrait) noexcept {
    if (device == EditorGameViewDevice::Tablet) {
        return EditorViewportProfileKind::Tablet;
    }
    return portrait ? EditorViewportProfileKind::PhonePortrait : EditorViewportProfileKind::PhoneLandscape;
}

} // namespace

EditorGameViewSize EditorGameView::Size(EditorGameViewDevice device, bool portrait) noexcept {
    const EditorGameViewSize landscape = EntryFor(device).landscape;
    return portrait && CanRotate(device) ? EditorGameViewSize{ landscape.height, landscape.width } : landscape;
}

bool EditorGameView::CanRotate(EditorGameViewDevice device) noexcept {
    return device != EditorGameViewDevice::Computer;
}

std::string_view EditorGameView::Name(EditorGameViewDevice device) noexcept {
    return EntryFor(device).name;
}

std::optional<EditorGameViewDevice> EditorGameView::FromName(std::string_view name) noexcept {
    for (const Entry& entry : kEntries) {
        if (entry.name == name) {
            return entry.device;
        }
    }
    return std::nullopt;
}

std::string EditorGameView::Label(EditorGameViewDevice device) {
    switch (device) {
    case EditorGameViewDevice::Computer:
        return std::string{ EditorText("gameView.device.computer", "Computer") };
    case EditorGameViewDevice::Tablet:
        return std::string{ EditorText("gameView.device.tablet", "Tablet") };
    case EditorGameViewDevice::Phone:
        return std::string{ EditorText("gameView.device.phone", "Phone") };
    }
    return {};
}

EditorGameViewSafeFrames EditorGameView::SafeFrames(EditorGameViewDevice device, bool portrait) noexcept {
    const EditorGameViewSize size = Size(device, portrait);
    if (device == EditorGameViewDevice::Computer) {
        const auto margins = [&size](double fraction) {
            const std::uint32_t horizontal = Scaled(size.width, fraction / 2.0);
            const std::uint32_t vertical = Scaled(size.height, fraction / 2.0);
            return EditorViewportSafeArea{ .left = horizontal, .top = vertical, .right = horizontal, .bottom = vertical };
        };
        return EditorGameViewSafeFrames{
            .frames = { EditorGameViewSafeFrame{ margins(kOuterComputerMargin), false }, EditorGameViewSafeFrame{ margins(kInnerComputerMargin), true } },
            .count = 2U,
        };
    }
    // The profile's insets are in the pixels of its own screen; they grow with the device's screen.
    const EditorViewportProfile& profile = EditorViewportProfileFor(ProfileKind(device, portrait));
    std::uint32_t profileWidth = profile.width;
    std::uint32_t profileHeight = profile.height;
    if (device == EditorGameViewDevice::Tablet && portrait) {
        std::swap(profileWidth, profileHeight);
    }
    const double scaleX = static_cast<double>(size.width) / static_cast<double>(profileWidth);
    const double scaleY = static_cast<double>(size.height) / static_cast<double>(profileHeight);
    return EditorGameViewSafeFrames{
        .frames = { EditorGameViewSafeFrame{ EditorViewportSafeArea{
            .left = Scaled(profile.safeArea.left, scaleX), .top = Scaled(profile.safeArea.top, scaleY),
            .right = Scaled(profile.safeArea.right, scaleX), .bottom = Scaled(profile.safeArea.bottom, scaleY) }, false } },
        .count = 1U,
    };
}

std::vector<EditorSceneViewportOverlayShape> EditorGameView::SafeFrameShapes(
    EditorGameViewDevice device, bool portrait, std::uint32_t shownWidth, std::uint32_t shownHeight) {
    const EditorGameViewSize size = Size(device, portrait);
    const EditorGameViewSafeFrames safe = SafeFrames(device, portrait);
    const float scaleX = static_cast<float>(shownWidth) / static_cast<float>(size.width);
    const float scaleY = static_cast<float>(shownHeight) / static_cast<float>(size.height);
    // A line is drawn around its points, so the outline keeps half its thickness inside the picture.
    constexpr float kThickness = 2.0F;
    constexpr float kHalf = kThickness / 2.0F;
    std::vector<EditorSceneViewportOverlayShape> shapes;
    for (std::size_t index = 0U; index < safe.count; ++index) {
        const EditorGameViewSafeFrame& frame = safe.frames[index];
        const float left = static_cast<float>(frame.insets.left) * scaleX + kHalf;
        const float top = static_cast<float>(frame.insets.top) * scaleY + kHalf;
        const float right = static_cast<float>(shownWidth) - static_cast<float>(frame.insets.right) * scaleX - kHalf;
        const float bottom = static_cast<float>(shownHeight) - static_cast<float>(frame.insets.bottom) * scaleY - kHalf;
        if (right <= left || bottom <= top) continue;
        shapes.push_back(EditorSceneViewportOverlayShape{
            .kind = EditorSceneViewportOverlayShape::Kind::Polyline,
            .points = { { left, top }, { right, top }, { right, bottom }, { left, bottom }, { left, top } },
            .thickness = kThickness,
            .color = frame.inner ? std::array<std::uint8_t, 4>{ 255U, 255U, 255U, 255U } : std::array<std::uint8_t, 4>{ 255U, 140U, 0U, 255U } });
    }
    return shapes;
}

std::string EditorGameView::FormatResolution(EditorGameViewSize size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

std::string EditorGameView::FormatScale(std::uint32_t shownWidth, std::uint32_t renderWidth) {
    const double scale = renderWidth == 0U ? 1.0 : static_cast<double>(shownWidth) / static_cast<double>(renderWidth);
    return std::to_string(std::lround(scale * 100.0)) + "%";
}

#if defined(_WIN32)
EditorGameViewFrame EditorGameView::Fit(const RECT& area, EditorGameViewDevice device, bool portrait) noexcept {
    const Entry& entry = EntryFor(device);
    const EditorGameViewSize size = Size(device, portrait);
    const double shortSide = static_cast<double>(std::min(size.width, size.height));
    // The device is the screen plus a bezel and a rim; the bezel grows with the screen, the rim does not.
    const double bezelPerScale = entry.bezel * shortSide;
    const double freeWidth = static_cast<double>(area.right - area.left) - 2.0 * (kMargin + entry.rim);
    const double freeHeight = static_cast<double>(area.bottom - area.top) - 2.0 * (kMargin + entry.rim);
    const double scale = std::min(freeWidth / (size.width + 2.0 * bezelPerScale), freeHeight / (size.height + 2.0 * bezelPerScale));
    if (!(scale > 0.0)) {
        return {};
    }
    const long screenWidth = std::lround(size.width * scale);
    const long screenHeight = std::lround(size.height * scale);
    if (screenWidth < 1 || screenHeight < 1) {
        return {};
    }
    const int bezel = static_cast<int>(std::lround(bezelPerScale * scale));
    const long bodyWidth = screenWidth + 2L * (bezel + entry.rim);
    const long bodyHeight = screenHeight + 2L * (bezel + entry.rim);
    const long bodyLeft = area.left + ((area.right - area.left) - bodyWidth) / 2;
    const long bodyTop = area.top + ((area.bottom - area.top) - bodyHeight) / 2;
    EditorGameViewFrame frame;
    frame.body = RECT{ bodyLeft, bodyTop, bodyLeft + bodyWidth, bodyTop + bodyHeight };
    frame.rim = entry.rim;
    frame.screen = RECT{ bodyLeft + bezel + entry.rim, bodyTop + bezel + entry.rim,
        bodyLeft + bezel + entry.rim + screenWidth, bodyTop + bezel + entry.rim + screenHeight };
    if (bezel > 0) {
        // The camera sits in the middle of the bezel on the short side that carries it: the left one while the
        // device lies, the top one while it stands.
        frame.cameraRadius = std::max(2, static_cast<int>(std::lround(bezel * 0.22)));
        frame.camera = portrait ? POINT{ (frame.screen.left + frame.screen.right) / 2, frame.screen.top - bezel / 2 }
                                : POINT{ frame.screen.left - bezel / 2, (frame.screen.top + frame.screen.bottom) / 2 };
    }
    return frame;
}
#endif

} // namespace kb::editor
