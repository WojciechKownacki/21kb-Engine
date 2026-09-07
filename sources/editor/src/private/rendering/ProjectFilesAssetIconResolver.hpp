#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct ProjectFilesAssetIcon {
    HeroIconKind kind = HeroIconKind::Cube;
    COLORREF color = RGB(174, 181, 193);
    int strokeWidth = 2;
};

class ProjectFilesAssetIconResolver {
public:
    ProjectFilesAssetIconResolver() = delete;

    [[nodiscard]] static ProjectFilesAssetIcon Resolve(const kb::assets::AssetMetadata& metadata, bool selected) noexcept;
    [[nodiscard]] static bool IsMaterial(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsMaterialGraph(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsMaterialType(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsTexture(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsMesh(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsSkeletalMesh(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsSkeleton(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsAnimationClip(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsAudio(const kb::assets::AssetMetadata& metadata) noexcept;
};

#endif

} // namespace kb::editor
