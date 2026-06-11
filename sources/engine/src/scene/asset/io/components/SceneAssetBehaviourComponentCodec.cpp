#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"

namespace kb::scene {

bool SceneAssetBehaviourComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, BehaviourComponent& output) {
    std::uint32_t backend = 0U;
    std::uint32_t tickGroup = 0U;
    bool enabled = true;
    if (!input.ReadUInt64(output.behaviourAssetId) ||
        !input.ReadUInt32(backend) ||
        backend > static_cast<std::uint32_t>(BehaviourBackend::VisualGraph) ||
        !input.ReadBool(enabled) ||
        !input.ReadUInt32(tickGroup) ||
        tickGroup > static_cast<std::uint32_t>(BehaviourTickGroup::Presentation) ||
        !input.ReadInt32(output.executionOrder)) {
        return false;
    }
    output.backend = static_cast<BehaviourBackend>(backend);
    output.enabled = enabled;
    output.tickGroup = static_cast<BehaviourTickGroup>(tickGroup);
    return true;
}

void SceneAssetBehaviourComponentCodec::Write(std::vector<std::uint8_t>& output, const BehaviourComponent& behaviour) {
    SceneAssetBinaryIO::WriteUInt64(output, behaviour.behaviourAssetId);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(behaviour.backend));
    SceneAssetBinaryIO::WriteUInt8(output, behaviour.enabled ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(behaviour.tickGroup));
    SceneAssetBinaryIO::WriteInt32(output, behaviour.executionOrder);
}

} // namespace kb::scene
