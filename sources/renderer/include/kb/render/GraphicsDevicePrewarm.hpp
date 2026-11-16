#pragma once

#include <memory>

namespace kb::render {

// Creating the Direct3D 11 device takes the graphics driver a large share of the time to a game's first frame, and it
// needs no window. A prewarm creates the device on a thread of its own so the host can load content meanwhile; the
// renderer adopts the device through DisplayConfig::preparedGraphicsDevice. Elsewhere Start does nothing.
class GraphicsDevicePrewarm final {
public:
    GraphicsDevicePrewarm();
    ~GraphicsDevicePrewarm();
    GraphicsDevicePrewarm(const GraphicsDevicePrewarm&) = delete;
    GraphicsDevicePrewarm& operator=(const GraphicsDevicePrewarm&) = delete;

    void Start();
    // True while a started prewarm has not handed its device over.
    [[nodiscard]] bool Pending() const noexcept;
    // True once the device exists or its creation failed, so Take does not wait.
    [[nodiscard]] bool Ready() const noexcept;
    // Waits for the device and returns it with one reference for the caller; null when none was made.
    [[nodiscard]] void* Take();
    // Drops a device that was made but never taken.
    static void Release(void* device) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::render
