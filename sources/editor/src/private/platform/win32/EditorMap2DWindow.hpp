#pragma once

#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/math/DVec3.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

class EditorSceneContext;
class EditorDockModel;
class EditorFloatingWindowManager;
struct EditorMetrics;

#if defined(_WIN32)

// Docked map canvas and palette, integrated with the scene Hierarchy, Inspector and command history.
class EditorMap2DWindow final {
public:
    EditorMap2DWindow() = delete;

    // Opens the editor of the entity's map, or brings its open window to the front.
    [[nodiscard]] static bool Open(
        HWND owner,
        EditorSceneContext& sceneContext,
        kb::scene::SceneEntity entity,
        std::function<void()> requestScenePresent);
    // Changes settings in the shared working copy and command history. Ctrl+S persists them.
    [[nodiscard]] static bool EditSettings(
        EditorSceneContext& sceneContext,
        kb::scene::SceneEntity entity,
        const std::function<void(kb::assets::Map2DAsset&)>& edit,
        std::string* error = nullptr);
    // The map as its open Map Editor holds it (unsaved strokes included), else as its file.
    [[nodiscard]] static std::optional<kb::assets::Map2DAsset> CurrentMap(const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity);
    // Closes every Map Editor, asking about unsaved maps; false when the user cancels.
    [[nodiscard]] static bool CloseAll(HWND owner);
    // Docked map canvas and palette share the main editor's Hierarchy and Inspector.
    static void SyncDocked(HWND parent, const RECT& bounds, EditorSceneContext& sceneContext);
    static void HideDocked(HWND parent);
    [[nodiscard]] static bool SaveAll(EditorSceneContext& sceneContext);
    [[nodiscard]] static bool PaintScenePointer(HWND source, HWND main, int x, int y, const EditorDockModel& docks,
        const EditorFloatingWindowManager& floating, const EditorMetrics& metrics, EditorSceneContext& context, bool begin);
    [[nodiscard]] static bool ScenePainting(const EditorSceneContext& context);
    [[nodiscard]] static std::vector<kb::scene::PhysicsDebugLineDesc> SceneBrushLines(const EditorSceneContext& context, const kb::math::DVec3& origin);
    [[nodiscard]] static bool SceneStrokeActive(EditorSceneContext& context);
    static void FinishSceneStroke(EditorSceneContext& context);
    // Automation: drives the editor of the entity's map as a user would, one input per line -
    // "click x y [ctrl|shift|alt...]", "drag x0 y0 x1 y1 [modifiers]", "move x y", "wheel x y delta
    // [modifiers]", "key <name> [modifiers]" (a letter, digit, space, delete, escape, pageup, pagedown,
    // left, right, up, down, f5, plus, minus, [, ]), "search text", "category name", "palette type",
    // "view flat|wall", "resize width height", "import project-relative/path.png", "discard"
    // or "tutorial" - then draws it into a PNG. The window is not
    // shown; what it painted stays in the scene unsaved unless the lines save it (key s ctrl).
    [[nodiscard]] static bool Drive(
        EditorSceneContext& sceneContext,
        kb::scene::SceneEntity entity,
        const std::vector<std::string>& inputs,
        const std::filesystem::path& png,
        int width,
        int height,
        std::string* error = nullptr);
    // Automation: the editor of the entity's map drawn into a PNG without showing a window.
    [[nodiscard]] static bool Capture(
        EditorSceneContext& sceneContext,
        kb::scene::SceneEntity entity,
        const std::filesystem::path& png,
        int width,
        int height,
        std::string* error = nullptr);
};

#endif

} // namespace kb::editor
