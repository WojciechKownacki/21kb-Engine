#include "scene/prefab/io/ScenePrefabAssetTemplateWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldWriter.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <ostream>

namespace kb::scene {

bool ScenePrefabAssetTemplateWriter::CanWrite(const ScenePrefabAssetWriteDesc& asset) {
    return asset.kind == ScenePrefabAssetKind::Template && asset.prefab != nullptr;
}

void ScenePrefabAssetTemplateWriter::WriteBody(std::ostream& output, const ScenePrefabAssetWriteDesc& asset) {
    output << ScenePrefabAssetFormat::NodesKey << '=' << asset.prefab->NodeCount() << '\n';
    for (const ScenePrefabNodeDesc& node : asset.prefab->Nodes()) {
        ScenePrefabAssetFieldWriter::WriteNode(output, node);
    }
}

} // namespace kb::scene
