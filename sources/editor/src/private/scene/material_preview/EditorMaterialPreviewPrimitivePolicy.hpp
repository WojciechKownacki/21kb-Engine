#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorMaterialPreviewPrimitiveKind : std::uint8_t {
    Sphere,
    Cylinder,
    Cube,
    Plane,
    CustomMesh,
    Fallback,
};

struct EditorMaterialPreviewPrimitivePolicy {
    EditorMaterialPreviewPrimitiveKind kind = EditorMaterialPreviewPrimitiveKind::Sphere;
    kb::assets::AssetId meshAssetId{};
    kb::assets::AssetId customMeshAssetId{};

    [[nodiscard]] static kb::assets::AssetId GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind kind) noexcept {
        switch (kind) {
        case EditorMaterialPreviewPrimitiveKind::Sphere:
            return kb::assets::MakeAssetId("EditorMaterialPreview:SphereMesh");
        case EditorMaterialPreviewPrimitiveKind::Cylinder:
            return kb::assets::MakeAssetId("EditorMaterialPreview:CylinderMesh");
        case EditorMaterialPreviewPrimitiveKind::Cube:
            return kb::assets::MakeAssetId("EditorMaterialPreview:CubeMesh");
        case EditorMaterialPreviewPrimitiveKind::Plane:
            return kb::assets::MakeAssetId("EditorMaterialPreview:PlaneMesh");
        case EditorMaterialPreviewPrimitiveKind::Fallback:
            return kb::assets::MakeAssetId("EditorMaterialPreview:FallbackCubeMesh");
        case EditorMaterialPreviewPrimitiveKind::CustomMesh:
            return {};
        }
        return {};
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy Sphere() noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = EditorMaterialPreviewPrimitiveKind::Sphere,
            .meshAssetId = GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Sphere),
        };
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy Cube() noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = EditorMaterialPreviewPrimitiveKind::Cube,
            .meshAssetId = GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Cube),
        };
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy Cylinder() noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = EditorMaterialPreviewPrimitiveKind::Cylinder,
            .meshAssetId = GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Cylinder),
        };
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy Plane() noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = EditorMaterialPreviewPrimitiveKind::Plane,
            .meshAssetId = GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane),
        };
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy CustomMesh(kb::assets::AssetId meshAssetId) noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = meshAssetId.IsValid() ? EditorMaterialPreviewPrimitiveKind::CustomMesh : EditorMaterialPreviewPrimitiveKind::Fallback,
            .meshAssetId = meshAssetId.IsValid() ? meshAssetId : GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Fallback),
            .customMeshAssetId = meshAssetId,
        };
    }

    [[nodiscard]] static EditorMaterialPreviewPrimitivePolicy Fallback() noexcept {
        return EditorMaterialPreviewPrimitivePolicy{
            .kind = EditorMaterialPreviewPrimitiveKind::Fallback,
            .meshAssetId = GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Fallback),
        };
    }
};

[[nodiscard]] inline std::string_view EditorMaterialPreviewPrimitiveName(EditorMaterialPreviewPrimitiveKind kind) noexcept {
    switch (kind) {
    case EditorMaterialPreviewPrimitiveKind::Sphere:
        return "Sphere";
    case EditorMaterialPreviewPrimitiveKind::Cylinder:
        return "Cylinder";
    case EditorMaterialPreviewPrimitiveKind::Cube:
        return "Cube";
    case EditorMaterialPreviewPrimitiveKind::Plane:
        return "Plane";
    case EditorMaterialPreviewPrimitiveKind::CustomMesh:
        return "Custom Mesh";
    case EditorMaterialPreviewPrimitiveKind::Fallback:
        return "Fallback";
    }
    return "Fallback";
}

} // namespace kb::editor
