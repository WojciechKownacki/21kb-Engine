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

    // Whether the keyboard and mouse belong to the game: the Game View is where the press last landed.
    // Stopping hands them back to the editor.
    [[nodiscard]] bool GameHasInput() const noexcept;
    void SetGameHasInput(bool hasInput) noexcept;

private:
    EditorPlayMode mode_ = EditorPlayMode::Stopped;
    bool gameHasInput_ = false;
    std::uint64_t generation_ = 0;
};

} // namespace kb::editor
