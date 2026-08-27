#pragma once

#include "engine/assets/AssetMetadata.hpp"

namespace kb::editor {

// Which assets the editor's open route has an editor for. The Project Files Open
// entry is offered exactly when this holds, so the menu can never present a command
// the route would decline.
class EditorAssetOpenPolicy {
public:
    EditorAssetOpenPolicy() = delete;

    [[nodiscard]] static bool IsSceneDocument(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static bool CanOpen(const kb::assets::AssetMetadata& metadata);
};

} // namespace kb::editor
