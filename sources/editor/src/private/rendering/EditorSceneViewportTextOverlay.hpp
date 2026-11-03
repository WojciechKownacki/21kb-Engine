#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kb::editor {

enum class EditorSceneViewportTextAlign : std::uint8_t { Left, Right };

// With Right, `x` is the right edge of the text. A label with a shadow is the readable-over-anything
// text of the bone names; one without sits on a panel of its own.
struct EditorSceneViewportTextLabel {
    float x = 0.0F;
    float y = 0.0F;
    float pixelHeight = 10.0F;
    std::string text;
    std::array<std::uint8_t, 4U> color{ 255U, 255U, 255U, 255U };
    EditorSceneViewportTextAlign align = EditorSceneViewportTextAlign::Left;
    bool shadow = true;
};

// A translucent panel or a line drawn under the labels. RoundedRect takes two opposite corners;
// Polyline takes any number of points and may be dashed.
struct EditorSceneViewportOverlayShape {
    enum class Kind : std::uint8_t { RoundedRect, Polyline };
    Kind kind = Kind::Polyline;
    std::vector<std::array<float, 2U>> points;
    float thickness = 1.0F;
    float radius = 0.0F;
    bool dashed = false;
    std::array<std::uint8_t, 4U> color{ 255U, 255U, 255U, 255U };
};

#if defined(_WIN32)

class EditorSceneViewportTextOverlay {
public:
    EditorSceneViewportTextOverlay();
    ~EditorSceneViewportTextOverlay();

    EditorSceneViewportTextOverlay(const EditorSceneViewportTextOverlay&) = delete;
    EditorSceneViewportTextOverlay& operator=(const EditorSceneViewportTextOverlay&) = delete;

    [[nodiscard]] bool Ensure(HINSTANCE instance, HWND parent, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool Update(std::span<const EditorSceneViewportTextLabel> labels,
        std::span<const EditorSceneViewportOverlayShape> shapes);
    // Draws the same content on a device context, inside `area` and with its corner as the origin.
    [[nodiscard]] bool Paint(HDC dc, const RECT& area, std::span<const EditorSceneViewportTextLabel> labels,
        std::span<const EditorSceneViewportOverlayShape> shapes);
    void Show() noexcept;
    void Hide() noexcept;
    void Destroy() noexcept;
    [[nodiscard]] HWND Window() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif

} // namespace kb::editor
