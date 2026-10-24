#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct GameActivityMotionEvent;

namespace kb::game {

class AndroidTouchInput {
public:
    [[nodiscard]] bool Process(const GameActivityMotionEvent& event) noexcept;
    void HandleAppCommand(std::int32_t command) noexcept;
    void SetViewport(std::uint32_t width, std::uint32_t height) noexcept;
    void Cancel() noexcept;
    void Publish(input::InputDeviceState& state) const noexcept;
    void FinishFrame() noexcept;

private:
    void Update(std::uint32_t id, float x, float y, input::InputTouchPhase phase) noexcept;
    std::array<input::InputTouchPoint, input::InputDeviceState::kMaxTouchPoints> points_{};
    std::size_t count_ = 0U;
    std::uint32_t width_ = 0U;
    std::uint32_t height_ = 0U;
};

} // namespace kb::game
