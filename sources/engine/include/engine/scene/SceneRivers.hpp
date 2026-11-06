#pragma once

#include <string>
#include <vector>

namespace kb::scene {

class Scene;

// Makes the water of every River of the scene: a child object that draws the stream, and one for each waterfall
// (with the mist effect at its foot), built from the river's points whenever they change. The children are
// transient render objects, never saved with the scene. Cheap when nothing changed (a comparison per river), so
// the renderer calls it before it synchronizes the scene, in the editor, the game and a capture alike.
// Returns what could not be built, one message each.
[[nodiscard]] std::vector<std::string> SynchronizeSceneRivers(Scene& scene);

} // namespace kb::scene
