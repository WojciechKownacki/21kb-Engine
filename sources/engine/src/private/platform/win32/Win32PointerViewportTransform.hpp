#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <utility>

namespace kb::input {

class Win32PointerViewportTransform final {
  public:
    Win32PointerViewportTransform(const RECT& clientViewport, std::uint32_t renderWidth,
                                  std::uint32_t renderHeight) noexcept
        : clientViewport_(clientViewport), renderWidth_(renderWidth), renderHeight_(renderHeight) {}

    [[nodiscard]] bool IsValid() const noexcept {
        return renderWidth_ > 0U && renderHeight_ > 0U && clientViewport_.right > clientViewport_.left &&
               clientViewport_.bottom > clientViewport_.top;
    }

    [[nodiscard]] float ScaleX() const noexcept {
        return IsValid()
                   ? static_cast<float>(renderWidth_) / static_cast<float>(clientViewport_.right - clientViewport_.left)
                   : 1.0F;
    }

    [[nodiscard]] float ScaleY() const noexcept {
        return IsValid() ? static_cast<float>(renderHeight_) /
                               static_cast<float>(clientViewport_.bottom - clientViewport_.top)
                         : 1.0F;
    }

    [[nodiscard]] std::pair<float, float> MapClientPoint(POINT point) const noexcept {
        if (!IsValid()) {
            return {static_cast<float>(point.x), static_cast<float>(point.y)};
        }
        return {
            static_cast<float>(point.x - clientViewport_.left) * ScaleX(),
            static_cast<float>(point.y - clientViewport_.top) * ScaleY(),
        };
    }

  private:
    RECT clientViewport_{};
    std::uint32_t renderWidth_ = 0U;
    std::uint32_t renderHeight_ = 0U;
};

} // namespace kb::input

#endif
