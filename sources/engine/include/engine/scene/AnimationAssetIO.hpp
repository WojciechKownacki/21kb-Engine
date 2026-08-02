#pragma once

#include "engine/scene/AnimationAssets.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace kb::scene {

inline constexpr const char* kAnimationClipAssetExtension = ".kbanim";
inline constexpr const char* kAnimationClipAssetType = "AnimationClip";
inline constexpr std::uint32_t kAnimationClipAssetSchemaVersion = 1U;
inline constexpr const char* kAnimatorControllerAssetExtension = ".kbanimcontroller";
inline constexpr const char* kAnimatorControllerAssetType = "AnimatorController";
inline constexpr std::uint32_t kAnimatorControllerAssetSchemaVersion = 1U;

class AnimationAssetIO final {
public:
    AnimationAssetIO() = delete;

    [[nodiscard]] static std::optional<AnimationClip> LoadClip(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<AnimatorController> LoadController(const std::filesystem::path& path);
    [[nodiscard]] static bool SaveClip(const std::filesystem::path& path, const AnimationClip& clip);
    [[nodiscard]] static bool SaveController(const std::filesystem::path& path, const AnimatorController& controller);
};

} // namespace kb::scene
