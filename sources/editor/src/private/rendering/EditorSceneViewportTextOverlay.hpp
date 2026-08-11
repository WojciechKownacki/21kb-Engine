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

namespace kb::editor {

struct EditorSceneViewportTextLabel {
    float x = 0.0F;
    float y = 0.0F;
    float pixelHeight = 10.0F;
    std::string text;
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
    [[nodiscard]] bool Update(std::span<const EditorSceneViewportTextLabel> labels);
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
