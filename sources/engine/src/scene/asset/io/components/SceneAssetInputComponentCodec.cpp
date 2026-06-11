#include "scene/asset/io/components/SceneAssetInputComponentCodec.hpp"

namespace kb::scene {

bool SceneAssetInputComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, InputComponent& output) {
    std::uint32_t priority = 0;
    if (!input.ReadUInt64(output.mappingContextAssetId) ||
        !input.ReadUInt32(priority) ||
        !input.ReadBool(output.enabled)) {
        return false;
    }
    output.priority = static_cast<std::int32_t>(priority);
    return true;
}

void SceneAssetInputComponentCodec::Write(std::vector<std::uint8_t>& output, const InputComponent& input) {
    SceneAssetBinaryIO::WriteUInt64(output, input.mappingContextAssetId);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(input.priority));
    SceneAssetBinaryIO::WriteUInt8(output, input.enabled ? 1U : 0U);
}

} // namespace kb::scene
