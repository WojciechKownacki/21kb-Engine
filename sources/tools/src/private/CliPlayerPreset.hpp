#pragma once

#include "CliCommon.hpp"

#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string_view>

namespace kb::scene { class Scene; }

namespace kb::cli {

// `entity add --create 'Platformer Player'` and 'Top Down Player': the Create items of a ready-made 2D player.
[[nodiscard]] bool IsPlayerPreset(std::string_view item) noexcept;

// Creates a player with a project-owned script, input defaults and a following camera.
// Existing scripts and input contexts are preserved. Native 2D physics is the default;
// box2d=false selects the 3D controller. Returns an invalid entity on failure.
[[nodiscard]] kb::scene::SceneEntity AddPlayerPreset(kb::scene::Scene& scene, const std::filesystem::path& projectRoot,
    std::string_view item, std::string_view name, CommandIo io, bool box2d = true);

} // namespace kb::cli
