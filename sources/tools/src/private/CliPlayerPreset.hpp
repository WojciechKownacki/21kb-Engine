#pragma once

#include "CliCommon.hpp"

#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string_view>

namespace kb::scene { class Scene; }

namespace kb::cli {

// `entity add --create 'Platformer Player'` and 'Top Down Player': the Create items of a ready-made 2D player.
[[nodiscard]] bool IsPlayerPreset(std::string_view item) noexcept;

// Makes the player in `scene` (the entity is not saved or parented here): a CharacterController, a behaviour that
// runs the project's copy of Platformer2DPlayer.lua or TopDown2DPlayer.lua (Assets/Scripts, written when missing and
// never overwritten), default Move and Jump input actions when the project has no input context yet, and, in every map
// of the scene, a camera that follows the player. What was done is printed to io.out. Invalid entity on failure.
[[nodiscard]] kb::scene::SceneEntity AddPlayerPreset(kb::scene::Scene& scene, const std::filesystem::path& projectRoot,
    std::string_view item, std::string_view name, CommandIo io);

} // namespace kb::cli
