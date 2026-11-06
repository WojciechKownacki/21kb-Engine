#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

// Makes the water of every River of the scene: a child object that draws the stream, and one for each waterfall
// (with the mist effect at its foot), built from the river's points whenever they change. The children are
// transient render objects, never saved with the scene. Cheap when nothing changed (a comparison per river), so
// the renderer calls it before it synchronizes the scene, in the editor, the game and a capture alike.
struct SceneRiverSynchronization {
    std::vector<std::string> errors;     // what could not be built, one message each
    std::vector<std::uint64_t> entities; // the water objects built, changed or removed by this call
};

[[nodiscard]] SceneRiverSynchronization SynchronizeSceneRivers(Scene& scene);

} // namespace kb::scene
