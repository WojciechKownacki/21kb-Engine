#pragma once

#include "TouchContactFrame.hpp"

#include <cstdint>

struct GameActivityMotionEvent;

namespace kb::game {

class AndroidTouchInput {
public:
    [[nodiscard]] bool Process(const GameActivityMotionEvent& event) noexcept;
    void HandleAppCommand(std::int32_t command) noexcept;
    void SetViewport(std::uint32_t width, std::uint32_t height) noexcept { frame_.SetViewport(width, height); }
    void Cancel() noexcept { frame_.Cancel(); }
    void Publish(input::InputDeviceState& state) const noexcept { frame_.Publish(state); }
    void FinishFrame() noexcept { frame_.FinishFrame(); }

private:
    TouchContactFrame frame_;
};

} // namespace kb::game
