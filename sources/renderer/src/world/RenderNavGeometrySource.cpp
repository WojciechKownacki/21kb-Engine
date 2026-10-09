#include "kb/render/world/RenderNavGeometrySource.hpp"

#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <memory>

namespace kb::render {

RenderNavGeometrySource::RenderNavGeometrySource(kb::assets::AssetManager& assets) noexcept
    : AssetNavGeometrySource(assets) {}

std::shared_ptr<const kb::navigation::NavSourceMesh> RenderNavGeometrySource::RenderMesh(std::uint64_t assetId) {
    const auto cached = renderMeshes_.find(assetId);
    if (cached != renderMeshes_.end()) {
        return cached->second;
    }
    // A terrain bakes from its exact heightfield rather than from its render chunks.
    std::shared_ptr<const kb::navigation::NavSourceMesh> result = TerrainMesh(assetId);
    if (result == nullptr) {
        const kb::assets::AssetHandle<RenderMeshAssetData> handle = Assets().Load<RenderMeshAssetData>(kb::assets::AssetId{ assetId });
        if (handle.IsLoaded()) {
            const RenderMeshAssetData& mesh = *handle.Shared();
            auto triangles = std::make_shared<kb::navigation::NavSourceMesh>();
            const std::size_t vertexCount = mesh.vertices.empty() ? mesh.tangentVertices.size() : mesh.vertices.size();
            triangles->vertices.reserve(vertexCount * 3U);
            for (std::size_t vertex = 0U; vertex < vertexCount; ++vertex) {
                if (!mesh.vertices.empty()) {
                    triangles->vertices.insert(triangles->vertices.end(), { mesh.vertices[vertex].x, mesh.vertices[vertex].y, mesh.vertices[vertex].z });
                } else {
                    const RenderStaticMeshVertexP3N3T4UV2& source = mesh.tangentVertices[vertex];
                    triangles->vertices.insert(triangles->vertices.end(), { source.x, source.y, source.z });
                }
            }
            const std::size_t indexCount = mesh.indices32.empty() ? mesh.indices16.size() : mesh.indices32.size();
            for (const RenderMeshSectionDesc& section : mesh.sections) {
                if (section.lodLevel != 0U || section.terrainLayerIndex != UINT8_MAX || section.indexCount < 3U) {
                    continue;
                }
                const std::size_t end = static_cast<std::size_t>(section.indexStart) + section.indexCount - section.indexCount % 3U;
                for (std::size_t index = section.indexStart; index + 3U <= end && index + 3U <= indexCount; index += 3U) {
                    std::uint32_t corners[3]{};
                    bool valid = true;
                    for (std::size_t corner = 0U; corner < 3U; ++corner) {
                        const std::uint32_t local = mesh.indices32.empty() ? mesh.indices16[index + corner] : mesh.indices32[index + corner];
                        const std::size_t global = static_cast<std::size_t>(section.vertexStart) + local;
                        valid = valid && global < vertexCount;
                        corners[corner] = static_cast<std::uint32_t>(global);
                    }
                    if (valid) {
                        triangles->indices.insert(triangles->indices.end(), { corners[0], corners[1], corners[2] });
                    }
                }
            }
            if (!triangles->indices.empty()) {
                result = std::move(triangles);
            }
        }
    }
    renderMeshes_.emplace(assetId, result);
    return result;
}

} // namespace kb::render
