#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "inspection/InspectorPanelState.hpp"

namespace kb::editor {

class EditorAudioInspectorDropPolicy {
public:
    EditorAudioInspectorDropPolicy() = delete;

    [[nodiscard]] static bool Accepts(
        const kb::assets::AssetMetadata& metadata,
        InspectorSectionId section,
        InspectorPropertyId property) noexcept;
};

} // namespace kb::editor
