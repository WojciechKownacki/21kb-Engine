#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

enum class InspectorSceneAudioRowKind : std::uint8_t {
    Asset,
    ReadOnly,
    Text,
    Bool,
    Action,
    Empty,
};

struct InspectorSceneAudioRow {
    InspectorSceneAudioRowKind kind = InspectorSceneAudioRowKind::ReadOnly;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int flatIndex = -1;
    std::string label;
    std::string value;
    std::string option;
    bool boolValue = false;
    bool invalid = false;
    InspectorDynamicRowIdentity identity;
};

class InspectorSceneAudioModel {
public:
    explicit InspectorSceneAudioModel(const kb::scene::Scene& scene);

    [[nodiscard]] std::span<const InspectorSceneAudioRow> Rows() const noexcept;
    [[nodiscard]] std::span<const InspectorSceneAudioRow> Rows(InspectorSectionId section) const noexcept;
    [[nodiscard]] const InspectorSceneAudioRow* Find(int flatIndex) const noexcept;
    [[nodiscard]] const InspectorSceneAudioRow* Find(
        int flatIndex,
        const InspectorDynamicRowIdentity& identity) const noexcept;
    [[nodiscard]] kb::assets::AssetId MixerId() const noexcept;
    [[nodiscard]] std::string Text() const;

    [[nodiscard]] static int RowHeight(InspectorSceneAudioRowKind kind) noexcept;
    [[nodiscard]] static bool ShouldDisplay(
        const kb::scene::Scene& scene,
        kb::assets::AssetId selectedAsset,
        kb::scene::SceneEntity selectedEntity) noexcept;

private:
    kb::assets::AssetId mixerId_{};
    std::vector<InspectorSceneAudioRow> rows_;
    std::size_t routingRows_ = 0U;
};

} // namespace kb::editor
