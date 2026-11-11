#pragma once
#include "engine/scene/SceneEntity.hpp"
#include <filesystem>
#include <iosfwd>
#include <string_view>
namespace kb::scene {
class Scene;
struct PlayerPresetOutput { std::ostream& out; std::ostream& err; };
[[nodiscard]] bool IsPlayerPreset(std::string_view item) noexcept;
// Writes what a player script needs into the project at `projectRoot`: the Move and Jump input assets (unless the
// project has an input context) and the preset script `scriptFile` under Assets/Scripts (unless it exists). `scene`
// has the project mounted.
[[nodiscard]] bool WritePlayerPresetFiles(Scene& scene, const std::filesystem::path& projectRoot, std::string_view scriptFile,
    PlayerPresetOutput output);
// Creates a component-based player and following camera, preserves existing scripts/input assets.
[[nodiscard]] SceneEntity AddPlayerPreset(Scene& scene, const std::filesystem::path& projectRoot,
    std::string_view item, std::string_view name, PlayerPresetOutput output, bool box2d = true);
} // namespace kb::scene
