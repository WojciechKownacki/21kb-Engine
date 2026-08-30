#include "scene/cache/SceneCachedDrawCommandMaterializer.hpp"

namespace kb::render {

void SceneCachedDrawCommandMaterializer::ApplyTemplate(const SceneCachedDrawCommand& cachedCommand, MeshDrawCommand& outCommand) noexcept {
    outCommand.pass = cachedCommand.key.pass;
    outCommand.meshAssetId = cachedCommand.key.meshAssetId;
    outCommand.materialAssetId = cachedCommand.key.materialAssetId;
    outCommand.sectionIndex = cachedCommand.key.sectionIndex;
    outCommand.materialSlot = cachedCommand.key.materialSlot;
    outCommand.firstMeshlet = cachedCommand.key.firstMeshlet;
    outCommand.meshletCount = cachedCommand.key.meshletCount;
    outCommand.indexStart = cachedCommand.key.indexStart;
    outCommand.indexCount = cachedCommand.key.indexCount;
    outCommand.vertexStart = cachedCommand.key.vertexStart;
    outCommand.vertexCount = cachedCommand.key.vertexCount;
    outCommand.lodLevel = cachedCommand.key.lodLevel;
    outCommand.terrainLayerIndex = cachedCommand.key.terrainLayerIndex;
    outCommand.mesh = cachedCommand.mesh;
    outCommand.material = cachedCommand.material;
    outCommand.meshResource = cachedCommand.meshResource;
    outCommand.materialResource = cachedCommand.materialResource;
    outCommand.state = cachedCommand.key.state;
}

} // namespace kb::render
