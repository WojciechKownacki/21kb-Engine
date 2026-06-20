#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/input/InputActionValueType.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorAssetBrowserState;
class EditorConsoleState;

// Creates and edits Input Action assets. Owns no I/O itself; all persistence is
// delegated to EditorInputAssetGateway. Single responsibility: Input Action
// authoring workflow + user feedback.
class EditorInputActionAuthoring {
public:
    EditorInputActionAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept;

    [[nodiscard]] bool Create(const std::filesystem::path& virtualFolder);
    // Creates an Input Axis: an action asset (axis extension) defaulting to Axis1D.
    [[nodiscard]] bool CreateAxis(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool SetName(kb::assets::AssetId id, std::string name);
    [[nodiscard]] bool CycleValueType(kb::assets::AssetId id);
    [[nodiscard]] bool SetValueType(kb::assets::AssetId id, kb::input::InputActionValueType valueType);
    [[nodiscard]] bool ToggleConsume(kb::assets::AssetId id);

private:
    EditorInputAssetGateway gateway_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
