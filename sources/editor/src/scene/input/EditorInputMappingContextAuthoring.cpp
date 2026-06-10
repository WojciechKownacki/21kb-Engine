#include "scene/input/EditorInputMappingContextAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/input/InputAssetIO.hpp"

#include <vector>

namespace kb::editor {

EditorInputMappingContextAuthoring::EditorInputMappingContextAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , catalog_(scene)
    , console_(console) {}

bool EditorInputMappingContextAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Input", "Could not resolve a physical folder for the new mapping context.");
        return false;
    }

    const std::filesystem::path path =
        EditorInputAssetGateway::UniqueFilePath(*folder, "NewInputMappingContext", kb::input::InputAssetFormat::ContextExtension);
    if (!gateway_.WriteNewContext(path, kb::input::InputMappingContextAsset{})) {
        console_.Error("Input", "Mapping context asset could not be written: " + path.generic_string());
        return false;
    }
    console_.Info("Input", "Mapping context asset created: " + path.generic_string());
    return true;
}

bool EditorInputMappingContextAuthoring::AddMapping(kb::assets::AssetId id) {
    return gateway_.MutateContext(id, [](kb::input::InputMappingContextAsset& asset) {
        asset.mappings.push_back(kb::input::InputKeyMapping{});
    });
}

bool EditorInputMappingContextAuthoring::RemoveMapping(kb::assets::AssetId id, std::size_t index) {
    return gateway_.MutateContext(id, [index](kb::input::InputMappingContextAsset& asset) {
        if (index < asset.mappings.size()) {
            asset.mappings.erase(asset.mappings.begin() + static_cast<std::ptrdiff_t>(index));
        }
    });
}

bool EditorInputMappingContextAuthoring::SetMappingKey(kb::assets::AssetId id, std::size_t index, kb::input::InputKey key) {
    return gateway_.MutateContext(id, [index, key](kb::input::InputMappingContextAsset& asset) {
        if (index < asset.mappings.size()) {
            asset.mappings[index].key = key;
        }
    });
}

bool EditorInputMappingContextAuthoring::SetMappingScale(kb::assets::AssetId id, std::size_t index, float scale) {
    return gateway_.MutateContext(id, [index, scale](kb::input::InputMappingContextAsset& asset) {
        if (index < asset.mappings.size()) {
            asset.mappings[index].scale = scale;
        }
    });
}

bool EditorInputMappingContextAuthoring::CycleMappingAction(kb::assets::AssetId id, std::size_t index) {
    const std::vector<std::uint64_t> actions = catalog_.SortedIdsOfType("InputAction");
    if (actions.empty()) {
        console_.Warning("Input", "No input action assets exist to assign.");
        return false;
    }
    return gateway_.MutateContext(id, [index, &actions](kb::input::InputMappingContextAsset& asset) {
        if (index < asset.mappings.size()) {
            asset.mappings[index].actionId = EditorInputAssetCatalog::NextCyclicId(actions, asset.mappings[index].actionId);
        }
    });
}

bool EditorInputMappingContextAuthoring::CycleMappingTrigger(kb::assets::AssetId id, std::size_t index) {
    return gateway_.MutateContext(id, [index](kb::input::InputMappingContextAsset& asset) {
        if (index >= asset.mappings.size()) {
            return;
        }
        std::vector<kb::input::InputTriggerDesc>& triggers = asset.mappings[index].triggers;
        if (triggers.empty()) {
            triggers.push_back(kb::input::InputTriggerDesc{ .type = kb::input::InputTriggerType::Pressed, .params = {}, .chordActionId = 0U });
            return;
        }
        const auto next = static_cast<std::uint8_t>(triggers.front().type) + 1U;
        if (next > static_cast<std::uint8_t>(kb::input::InputTriggerType::Chorded)) {
            // Wrap back to an implicit "Down" by clearing the explicit trigger stack.
            triggers.clear();
            return;
        }
        triggers.front().type = static_cast<kb::input::InputTriggerType>(next);
    });
}

} // namespace kb::editor
