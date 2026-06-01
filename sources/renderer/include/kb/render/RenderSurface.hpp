#pragma once

#include <cstdint>

namespace kb::render {

class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    [[nodiscard]] virtual std::uint32_t Width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t Height() const noexcept = 0;
    [[nodiscard]] virtual void* NativeWindowHandle() const noexcept = 0;
    [[nodiscard]] virtual void* NativeDisplayHandle() const noexcept = 0;
};

} // namespace kb::render
