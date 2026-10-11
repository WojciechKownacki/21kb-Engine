#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::render {

class RenderMeshAssetFinalizer final {
public:
    // OBJ, glTF and FBX geometry is right-handed with +Y up; the engine is left-handed
    // (+Y up, +Z forward, +X right). Every source importer mirrors source X before
    // Finalize - the conversion Unity applies, so a model keeps its front at +Z and is
    // not drawn mirrored - and reverses the triangle winding the mirror turned over.
    static void ConvertRightHandedSource(RenderMeshAssetData& asset) noexcept;
    // The import scale and rotation (AssetMeshImportTransform) on the converted engine-space data:
    // positions rotate and scale, normals and tangents rotate. A uniform positive scale keeps the
    // winding and the unit normals.
    static void ApplyImportTransform(RenderMeshAssetData& asset, const kb::assets::AssetMeshImportTransform& transform) noexcept;
    static void EnsureTangentVertexStorage(RenderMeshAssetData& asset);
    [[nodiscard]] static bool Finalize(
        RenderMeshAssetData& asset,
        const RenderMeshFinalizeOptions& options = {});
};

} // namespace kb::render
