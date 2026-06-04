#include "app/EditorPlayModeState.hpp"

namespace kb::editor {

EditorPlayMode EditorPlayModeState::Mode() const noexcept {
    return mode_;
}

bool EditorPlayModeState::IsPlaying() const noexcept {
    return mode_ == EditorPlayMode::Playing;
}

bool EditorPlayModeState::IsPaused() const noexcept {
    return mode_ == EditorPlayMode::Paused;
}

std::uint64_t EditorPlayModeState::Generation() const noexcept {
    return generation_;
}

void EditorPlayModeState::Play() noexcept {
    if (mode_ == EditorPlayMode::Playing) {
        return;
    }
    mode_ = EditorPlayMode::Playing;
    ++generation_;
}

void EditorPlayModeState::Stop() noexcept {
    if (mode_ == EditorPlayMode::Stopped) {
        return;
    }
    mode_ = EditorPlayMode::Stopped;
    ++generation_;
}

void EditorPlayModeState::Pause() noexcept {
    if (mode_ != EditorPlayMode::Playing) {
        return;
    }
    mode_ = EditorPlayMode::Paused;
    ++generation_;
}

void EditorPlayModeState::Resume() noexcept {
    Play();
}

} // namespace kb::editor
