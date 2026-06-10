#pragma once

namespace kb::scene {

class Scene;

// Bridges InputComponents to the runtime input subsystem. Call Apply when play
// begins to push every enabled entity's mapping context (by priority); call
// Clear when play stops to remove them all.
struct SceneInputActivation {
    SceneInputActivation() = delete;

    static void Apply(Scene& scene);
    static void Clear(Scene& scene);
};

} // namespace kb::scene
