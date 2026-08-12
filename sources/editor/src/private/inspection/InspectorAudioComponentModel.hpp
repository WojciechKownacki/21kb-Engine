#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
struct AudioSourceComponent;
}

namespace kb::editor {

enum class InspectorAudioControlKind : std::uint8_t {
    Asset,
    Float,
    Bool,
    Enum,
    Integer,
    Route,
    Action,
};

struct InspectorAudioRow {
    InspectorPropertyId property = InspectorPropertyId::None;
    InspectorAudioControlKind kind = InspectorAudioControlKind::Float;
    std::string_view label;
    InspectorPropertyId pickerProperty = InspectorPropertyId::None;
};

enum class InspectorAudioRouteStatus : std::uint8_t {
    MasterOnly,
    Available,
    Unavailable,
};

struct InspectorAudioRouteChoices {
    InspectorAudioRouteStatus status = InspectorAudioRouteStatus::MasterOnly;
    std::vector<std::string> names;
};

class InspectorAudioComponentModel {
public:
    InspectorAudioComponentModel() = delete;

    [[nodiscard]] static std::span<const InspectorAudioRow> SourceRows() noexcept;
    [[nodiscard]] static std::span<const InspectorAudioRow> ListenerRows() noexcept;
    [[nodiscard]] static const InspectorAudioRow* FindRow(InspectorPropertyId property) noexcept;
    [[nodiscard]] static InspectorHitKind HitKindForControl(InspectorAudioControlKind kind) noexcept;
    [[nodiscard]] static bool HasRemoveControl(InspectorSectionId section) noexcept;
    [[nodiscard]] static bool RemoveComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section) noexcept;

    [[nodiscard]] static bool IsFloatProperty(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool IsIntegerProperty(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool ReadFloat(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, float& value) noexcept;
    [[nodiscard]] static bool ReadInteger(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::int64_t& value) noexcept;
    [[nodiscard]] static bool ApplyFloat(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, float value) noexcept;
    [[nodiscard]] static bool ApplyInteger(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::int64_t value) noexcept;
    [[nodiscard]] static bool ApplyText(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) noexcept;
    [[nodiscard]] static bool Toggle(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool CycleAttenuation(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static std::string ClipDisplay(const kb::scene::Scene& scene, const kb::scene::AudioSourceComponent& source);

    [[nodiscard]] static InspectorAudioRouteChoices RouteChoices(const kb::scene::Scene& scene);
    [[nodiscard]] static bool SetOutputBus(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view busName);
    [[nodiscard]] static bool CycleOutputBus(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static std::string OutputBusDisplay(const kb::scene::Scene& scene, const kb::scene::AudioSourceComponent& source);
};

} // namespace kb::editor
