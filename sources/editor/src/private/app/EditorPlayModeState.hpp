#pragma once

#include <cstdint>

namespace kb::editor {

enum class EditorPlayMode : std::uint8_t {
    Stopped,
    Playing,
    Paused,
};

class EditorPlayModeState {
public:
    [[nodiscard]] EditorPlayMode Mode() const noexcept;
    [[nodiscard]] bool IsPlaying() const noexcept;
    [[nodiscard]] bool IsPaused() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;

    void Play() noexcept;
    void Stop() noexcept;
    void Pause() noexcept;
    void Resume() noexcept;

private:
    EditorPlayMode mode_ = EditorPlayMode::Stopped;
    std::uint64_t generation_ = 0;
};

} // namespace kb::editor
