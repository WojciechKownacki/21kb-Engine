#include "rendering/ProjectFilesAssetIconResolver.hpp"

#if defined(_WIN32)
namespace kb::editor {
namespace {

[[nodiscard]] bool IsPrefab(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "ScenePrefab" || metadata.virtualPath.extension() == ".kbprefab";
}

[[nodiscard]] bool IsScript(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "LuaScript"
        || metadata.type == "Script"
        || metadata.importCategory == "Script"
        || metadata.virtualPath.extension() == ".lua";
}

[[nodiscard]] bool IsInputAction(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "InputAction";
}

[[nodiscard]] bool IsInputMapping(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "InputMappingContext";
}

[[nodiscard]] COLORREF AssetNeutral(bool selected) noexcept {
    return selected ? RGB(205, 211, 221) : RGB(174, 181, 193);
}

} // namespace

bool ProjectFilesAssetIconResolver::IsTexture(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "Texture" || metadata.importCategory == "Texture";
}

bool ProjectFilesAssetIconResolver::IsMesh(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMesh" || metadata.importCategory == "Mesh";
}

ProjectFilesAssetIcon ProjectFilesAssetIconResolver::Resolve(const kb::assets::AssetMetadata& metadata, bool selected) noexcept {
    if (IsPrefab(metadata)) {
        return ProjectFilesAssetIcon{ .kind = HeroIconKind::Cube, .color = selected ? RGB(106, 177, 255) : RGB(68, 145, 236), .strokeWidth = 2 };
    }
    if (IsScript(metadata)) {
        return ProjectFilesAssetIcon{ .kind = HeroIconKind::DocumentText, .color = selected ? RGB(220, 235, 255) : RGB(142, 190, 255), .strokeWidth = 2 };
    }
    if (IsInputAction(metadata)) {
        return ProjectFilesAssetIcon{ .kind = HeroIconKind::Bolt, .color = selected ? RGB(255, 234, 160) : RGB(242, 193, 78), .strokeWidth = 2 };
    }
    if (IsInputMapping(metadata)) {
        return ProjectFilesAssetIcon{ .kind = HeroIconKind::RectangleGroup, .color = selected ? RGB(198, 240, 220) : RGB(108, 204, 164), .strokeWidth = 2 };
    }
    return ProjectFilesAssetIcon{ .kind = HeroIconKind::Cube, .color = AssetNeutral(selected), .strokeWidth = 2 };
}

} // namespace kb::editor

#endif
