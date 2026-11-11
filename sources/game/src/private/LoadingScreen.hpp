#pragma once

#include "RuntimeSceneFrameSync.hpp"

#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <chrono>
#include <iosfwd>
#include <memory>
#include <vector>

namespace kb::render {
class Renderer;
}

namespace kb::scene {
class Scene;
}

namespace kb::game {

struct GameProjectRuntime;

// The screen a shipped game shows while a scene loads: the project's launch image on its background colour with a bar
// under it that follows the load. It is a small scene of its own, made of the same UI elements any scene uses and
// drawn by the same renderer, so it can be on screen before the game's own scene exists and while a scene change
// replaces that one.
class LoadingScreen {
public:
    LoadingScreen();
    ~LoadingScreen();
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    // Builds the screen from the project's launch settings. The image the project names comes from its package, or
    // from its folder when the game runs from one; the engine's own mark is shown when it names none.
    [[nodiscard]] bool Initialize(const GameProjectRuntime& project, std::ostream& err);

    // Follows the load of `loading` (none before the scene exists): `fraction` is how far it has got, from 0 to 1.
    // The first report of a load shows the screen and each report draws one frame of it. The report of 1 draws two
    // frames with the loaded scene drawn hidden behind the screen, so its programs and pipeline states are made
    // before it shows. After it the screen stays up, still drawing the scene hidden, until the minimum display time
    // has passed, drawn by PresentHeld.
    void Report(kb::render::Renderer& renderer, float fraction, const kb::scene::Scene* loading);

    // True while a finished load is still being held for the minimum display time. Draws the screen in place of
    // the game's frame then; the host skips its own frame when this returns true.
    [[nodiscard]] bool PresentHeld(kb::render::Renderer& renderer);

    // Frees what the renderer holds for the screen.
    void Release(kb::render::Renderer& renderer);

    // Every progress value drawn, in order, with the frame it was drawn in. Hosts that record a run read it.
    struct DrawnFrame {
        float progress = 0.0F;
        bool held = false;
        // The loaded scene was drawn hidden behind the screen in this frame.
        bool prewarm = false;
    };
    [[nodiscard]] const std::vector<DrawnFrame>& DrawnFrames() const noexcept { return drawn_; }
    [[nodiscard]] bool Showing() const noexcept { return showing_; }

private:
    bool Draw(kb::render::Renderer& renderer, float progress, bool held, const kb::scene::Scene* prewarmed);

    std::unique_ptr<kb::scene::Scene> scene_;
    RuntimeSceneFrameSync sync_;
    kb::project::ProjectSplashSettings settings_;
    kb::scene::SceneEntity fill_{};
    kb::scene::SceneEntity track_{};
    // The scene a finished load placed, drawn hidden behind the screen while it stays up.
    const kb::scene::Scene* prewarmed_ = nullptr;
    bool showing_ = false;
    bool finished_ = false;
    float progress_ = 0.0F;
    std::chrono::steady_clock::time_point shownAt_{};
    std::vector<DrawnFrame> drawn_;
};

} // namespace kb::game
