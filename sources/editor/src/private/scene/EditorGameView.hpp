#pragma once

#include "app/EditorPlayPreferences.hpp"
#include "rendering/EditorSceneViewportTextOverlay.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorGameViewSize {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    [[nodiscard]] bool operator==(const EditorGameViewSize&) const noexcept = default;
};

#if defined(_WIN32)
// Where a device's screen and body are inside an area, in the same coordinates as the area.
struct EditorGameViewFrame {
    RECT screen{};
    // The outer edge of the device: the screen with its bezel and metal rim. A computer has only a thin rim.
    RECT body{};
    int rim = 0;
    // The camera dot in the bezel; a radius of zero means the device has none.
    POINT camera{};
    int cameraRadius = 0;
};
#endif

// The Game View's devices: the size and shape of each one's screen and how the panel draws it.
class EditorGameView final {
public:
    EditorGameView() = delete;

    static constexpr std::array<EditorGameViewDevice, 3U> kDevices{
        EditorGameViewDevice::Computer,
        EditorGameViewDevice::Tablet,
        EditorGameViewDevice::Phone,
    };

    // The render size of a device; a computer always lies, so its orientation does not matter.
    [[nodiscard]] static EditorGameViewSize Size(EditorGameViewDevice device, bool portrait) noexcept;
    // Whether the device can be turned: tablets and phones.
    [[nodiscard]] static bool CanRotate(EditorGameViewDevice device) noexcept;
    // Stable text for the editor settings file.
    [[nodiscard]] static std::string_view Name(EditorGameViewDevice device) noexcept;
    [[nodiscard]] static std::optional<EditorGameViewDevice> FromName(std::string_view name) noexcept;
    // The device as the bar names it, in the editor language.
    [[nodiscard]] static std::string Label(EditorGameViewDevice device);
    // Whether the device has edges the game should keep its UI away from: tablets and phones, with their
    // notches, rounded corners and system bars. A computer's whole screen is safe.
    [[nodiscard]] static bool HasSafeArea(EditorGameViewDevice device) noexcept;
    // The one safe area, as the margins it keeps from the edges in pixels of the device's screen; none on a computer.
    [[nodiscard]] static EditorViewportSafeArea SafeArea(EditorGameViewDevice device, bool portrait) noexcept;
    // The parts of a picture shown shownWidth wide and shownHeight high that lie outside the safe area, as
    // translucent red bands (at most four): the game's UI should stay off them.
    [[nodiscard]] static std::vector<EditorSceneViewportOverlayShape> UnsafeAreaShapes(
        EditorGameViewDevice device, bool portrait, std::uint32_t shownWidth, std::uint32_t shownHeight);
    // The screen's pixels as the bar reads them: "2340x1080".
    [[nodiscard]] static std::string FormatResolution(EditorGameViewSize size);
    // How large the picture appears next to the pixels it renders: "79%".
    [[nodiscard]] static std::string FormatScale(std::uint32_t shownWidth, std::uint32_t renderWidth);
#if defined(_WIN32)
    // The largest device that fits the area, centered, with the screen in the screen's own shape. An area
    // too small for a picture gives an empty screen.
    [[nodiscard]] static EditorGameViewFrame Fit(const RECT& area, EditorGameViewDevice device, bool portrait) noexcept;
#endif
};

} // namespace kb::editor
