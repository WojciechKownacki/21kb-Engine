#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorSceneContext;

// The splat map sizes of the drawers: the ones IsTerrainSplatResolution accepts.
inline constexpr std::array<std::uint32_t, 3U> kEditorTerrainSplatResolutions{ 512U, 1024U, 2048U };

enum class EditorTerrainJobKind : std::uint8_t {
    None,
    World,
    Thermal,
    Hydraulic,
    Settings,
};

// The one long terrain job the editor runs at a time (a new world, an erosion). It works on its own copy
// of the terrain on a worker thread, so the editor keeps running and the terrain on screen is untouched
// until the job has succeeded; the result then replaces the terrain as one Undo step.
class EditorTerrainJobs final {
public:
    EditorTerrainJobs() = delete;

    [[nodiscard]] static EditorTerrainJobKind Active() noexcept;
    // 0..1.
    [[nodiscard]] static float Progress() noexcept;

    // False with the reason when a job is already running or the entity has no terrain.
    [[nodiscard]] static bool StartWorld(
        EditorSceneContext& context, kb::scene::SceneEntity entity, std::string_view preset, std::uint32_t seed, float character, std::string* error);
    [[nodiscard]] static bool StartErosion(
        EditorSceneContext& context, kb::scene::SceneEntity entity, EditorTerrainJobKind kind, std::string* error);
    // The settings of the Advanced drawer: the splat map resolution (0 keeps it; the map is resampled), the chunk
    // level of detail error in pixels and the collision step. Applied as one Undo step like the other jobs.
    [[nodiscard]] static bool StartSettings(
        EditorSceneContext& context, kb::scene::SceneEntity entity, std::uint32_t splatResolution, float lodPixelError, std::uint32_t collisionStep,
        std::string* error);
    // Asks the running job to stop; it delivers nothing.
    static void Cancel() noexcept;
    // Once per frame: delivers a finished job, or reports why it delivered nothing. True when what the
    // screen shows changed (the progress moved, the job ended).
    [[nodiscard]] static bool Poll(EditorSceneContext& context);
    // Waits for the running job (at most `seconds`, then stops it) and delivers it; false with the reason
    // when it failed, was stopped, or its result could not replace the terrain.
    [[nodiscard]] static bool Finish(EditorSceneContext& context, double seconds, std::string* error);
};

} // namespace kb::editor
