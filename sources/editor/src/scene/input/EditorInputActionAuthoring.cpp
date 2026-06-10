#include "scene/input/EditorInputActionAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/input/InputAssetIO.hpp"

#include <utility>

namespace kb::editor {

EditorInputActionAuthoring::EditorInputActionAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , console_(console) {}

bool EditorInputActionAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Input", "Could not resolve a physical folder for the new input action.");
        return false;
    }

    const std::filesystem::path path =
        EditorInputAssetGateway::UniqueFilePath(*folder, "NewInputAction", kb::input::InputAssetFormat::ActionExtension);
    kb::input::InputActionAsset asset;
    asset.name = path.stem().string();
    if (!gateway_.WriteNewAction(path, asset)) {
        console_.Error("Input", "Input action asset could not be written: " + path.generic_string());
        return false;
    }
    console_.Info("Input", "Input action asset created: " + path.generic_string());
    return true;
}

bool EditorInputActionAuthoring::SetName(kb::assets::AssetId id, std::string name) {
    return gateway_.MutateAction(id, [&name](kb::input::InputActionAsset& asset) {
        asset.name = name.empty() ? std::string{ "Action" } : name;
    });
}

bool EditorInputActionAuthoring::CycleValueType(kb::assets::AssetId id) {
    return gateway_.MutateAction(id, [](kb::input::InputActionAsset& asset) {
        switch (asset.valueType) {
        case kb::input::InputActionValueType::Bool:
            asset.valueType = kb::input::InputActionValueType::Axis1D;
            break;
        case kb::input::InputActionValueType::Axis1D:
            asset.valueType = kb::input::InputActionValueType::Axis2D;
            break;
        case kb::input::InputActionValueType::Axis2D:
            asset.valueType = kb::input::InputActionValueType::Axis3D;
            break;
        case kb::input::InputActionValueType::Axis3D:
            asset.valueType = kb::input::InputActionValueType::Bool;
            break;
        }
    });
}

bool EditorInputActionAuthoring::ToggleConsume(kb::assets::AssetId id) {
    return gateway_.MutateAction(id, [](kb::input::InputActionAsset& asset) {
        asset.consumeInput = !asset.consumeInput;
    });
}

} // namespace kb::editor
