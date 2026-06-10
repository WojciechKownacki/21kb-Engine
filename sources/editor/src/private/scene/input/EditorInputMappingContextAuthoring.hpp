#pragma once

#include "engine/assets/AssetId.hpp"
#include "scene/input/EditorInputAssetCatalog.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::input {

enum class InputKey : std::uint16_t;

} // namespace kb::input

namespace kb::editor {

class EditorAssetBrowserState;
class EditorConsoleState;

// Creates and edits Input Mapping Context assets and the key->action mappings
// inside them. Single responsibility: Input Mapping Context authoring workflow.
class EditorInputMappingContextAuthoring {
public:
    EditorInputMappingContextAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept;

    [[nodiscard]] bool Create(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool AddMapping(kb::assets::AssetId id);
    [[nodiscard]] bool RemoveMapping(kb::assets::AssetId id, std::size_t index);
    [[nodiscard]] bool SetMappingKey(kb::assets::AssetId id, std::size_t index, kb::input::InputKey key);
    [[nodiscard]] bool CycleMappingAction(kb::assets::AssetId id, std::size_t index);
    [[nodiscard]] bool CycleMappingTrigger(kb::assets::AssetId id, std::size_t index);

private:
    EditorInputAssetGateway gateway_;
    EditorInputAssetCatalog catalog_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
