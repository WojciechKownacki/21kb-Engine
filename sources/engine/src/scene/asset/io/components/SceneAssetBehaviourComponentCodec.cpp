#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"

#include "engine/scene/SceneDocument.hpp"
#include "engine/script/ScriptValue.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace kb::scene {

bool SceneAssetBehaviourComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, BehaviourComponent& output,
    std::vector<BehaviourVariableOverride>& variableOverrides) {
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
    variableOverrides.clear();
    if (fileVersion < SceneDocument::BehaviourVariableOverridesFileVersion) {
        return true;
    }
    std::uint32_t count = 0U;
    if (!input.ReadUInt32(count) || count > SceneAssetFormat::MaxBehaviourVariableOverrides) {
        return false;
    }
    variableOverrides.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        BehaviourVariableOverride entry;
        std::string valueText;
        if (!input.ReadString(entry.name) || entry.name.empty() || !input.ReadString(valueText)) {
            return false;
        }
        std::optional<kb::script::ScriptValue> value = kb::script::TryParseTypedValueText(valueText);
        if (!value.has_value() ||
            std::ranges::any_of(variableOverrides, [&entry](const BehaviourVariableOverride& other) { return other.name == entry.name; })) {
            return false;
        }
        entry.value = std::move(*value);
        variableOverrides.push_back(std::move(entry));
    }
    return true;
}

void SceneAssetBehaviourComponentCodec::Write(std::vector<std::uint8_t>& output, const BehaviourComponent& behaviour,
    std::span<const BehaviourVariableOverride> variableOverrides) {
    SceneAssetBinaryIO::WriteUInt64(output, behaviour.behaviourAssetId);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(behaviour.backend));
    SceneAssetBinaryIO::WriteUInt8(output, behaviour.enabled ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(behaviour.tickGroup));
    SceneAssetBinaryIO::WriteInt32(output, behaviour.executionOrder);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(variableOverrides.size()));
    for (const BehaviourVariableOverride& entry : variableOverrides) {
        SceneAssetBinaryIO::WriteString(output, entry.name);
        SceneAssetBinaryIO::WriteString(output, kb::script::FormatTypedValueText(entry.value));
    }
}

} // namespace kb::scene
