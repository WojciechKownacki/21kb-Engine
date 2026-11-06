#pragma once

#include "engine/scene/RiverGeometry.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <vector>

namespace kb::editor {

class EditorSceneContext;

// The terrain bar's River tool: clicks on the terrain lay the points of a River, dragging a point moves it,
// Ctrl+click removes it, and Cut Channel digs the bed. Every change is one Undo step.
class EditorRiverTool final {
public:
    EditorRiverTool() = delete;

    // The river the tool edits: the one it made or last edited, else the first of the scene; invalid when
    // the scene has none.
    [[nodiscard]] static kb::scene::SceneEntity ActiveRiver(const EditorSceneContext& sceneContext);

    // The button went down on the terrain `terrain` at (x, z) of its plane: lays a point, picks one up to drag,
    // or (remove) deletes one. The first click of a new river waits for the second.
    [[nodiscard]] static bool Press(EditorSceneContext& sceneContext, kb::scene::SceneEntity terrain, float x, float z, bool remove);
    // The pointer moved with a point picked up.
    [[nodiscard]] static bool Drag(EditorSceneContext& sceneContext, kb::scene::SceneEntity terrain, float x, float z);
    // The button went up: a dragged point is put down as one Undo step.
    static void Release(EditorSceneContext& sceneContext);

    // Cuts the active river's bed and banks into the terrain.
    [[nodiscard]] static bool Carve(EditorSceneContext& sceneContext, kb::scene::SceneEntity terrain);

    // The river as the overlay draws it, in the space of the terrain: the line along the water, and the points
    // (x, y, z) the user placed. False when the river or the terrain is gone.
    [[nodiscard]] static bool TerrainView(
        const EditorSceneContext& sceneContext, kb::scene::SceneEntity terrain, kb::scene::SceneEntity river,
        std::vector<kb::scene::RiverSample>& line, std::vector<kb::scene::Vec3>& points);
};

} // namespace kb::editor
