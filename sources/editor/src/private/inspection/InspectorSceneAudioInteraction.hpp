#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "inspection/InspectorSceneAudioModel.hpp"

#include <optional>
#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

struct InspectorSceneAudioTarget {
    InspectorHitKind kind = InspectorHitKind::None;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int index = -1;
};

enum class InspectorSceneAudioCommandKind : std::uint8_t {
    SetMixer,
    SetSnapshot,
    SetOcclusion,
};

struct InspectorSceneAudioCommand {
    InspectorSceneAudioCommandKind kind = InspectorSceneAudioCommandKind::SetOcclusion;
    kb::assets::AssetId mixer{};
    std::string snapshot;
    kb::scene::AudioOcclusionSettings occlusion{};
};

class InspectorSceneAudioInteraction {
public:
    InspectorSceneAudioInteraction() = delete;

    [[nodiscard]] static bool IsSection(InspectorSectionId section) noexcept;
    [[nodiscard]] static bool IsTextProperty(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool BeginTextEdit(
        InspectorPanelState& state,
        const InspectorSceneAudioModel& model,
        const InspectorSceneAudioTarget& target,
        std::uint64_t documentGeneration);
    [[nodiscard]] static std::optional<InspectorSceneAudioCommand> ResolveAction(
        const kb::scene::Scene& scene,
        const InspectorSceneAudioModel& model,
        const InspectorSceneAudioTarget& target);
    [[nodiscard]] static std::optional<InspectorSceneAudioCommand> ResolvePicker(
        bool accepted,
        kb::assets::AssetId mixer) noexcept;
    [[nodiscard]] static std::optional<kb::assets::AssetId> ResolveReveal(
        const InspectorSceneAudioModel& model,
        const InspectorSceneAudioTarget& target) noexcept;
    [[nodiscard]] static std::optional<InspectorSceneAudioCommand> ResolveCommit(
        const InspectorPanelState& state,
        const kb::scene::Scene& scene,
        const InspectorSceneAudioModel& model,
        std::uint64_t documentGeneration);

    template <typename Actions>
    [[nodiscard]] static bool Apply(Actions& actions, const InspectorSceneAudioCommand& command) {
        switch (command.kind) {
        case InspectorSceneAudioCommandKind::SetMixer:
            return actions.SetSceneAudioMixer(command.mixer);
        case InspectorSceneAudioCommandKind::SetSnapshot:
            return actions.SetSceneAudioSnapshot(command.snapshot);
        case InspectorSceneAudioCommandKind::SetOcclusion:
            return actions.SetSceneAudioOcclusion(command.occlusion);
        }
        return false;
    }

    template <typename Actions>
    [[nodiscard]] static bool HandlePointerDown(
        InspectorPanelState& state,
        const kb::scene::Scene& scene,
        const InspectorSceneAudioModel& model,
        const InspectorSceneAudioTarget& target,
        Actions& actions,
        std::uint64_t documentGeneration) {
        if (BeginTextEdit(state, model, target, documentGeneration)) {
            return true;
        }
        const InspectorSceneAudioRow* row = model.Find(target.index);
        if (row == nullptr || row->section != target.section || row->property != target.property) {
            return false;
        }
        state.EndTextEdit();
        if (const std::optional<InspectorSceneAudioCommand> command = ResolveAction(scene, model, target)) {
            static_cast<void>(Apply(actions, *command));
        }
        return true;
    }

    template <typename Actions>
    [[nodiscard]] static bool CommitTextEdit(
        InspectorPanelState& state,
        const kb::scene::Scene& scene,
        const InspectorSceneAudioModel& model,
        Actions& actions,
        std::uint64_t documentGeneration) {
        const std::optional<InspectorSceneAudioCommand> command =
            ResolveCommit(state, scene, model, documentGeneration);
        state.EndTextEdit();
        return command.has_value() && Apply(actions, *command);
    }
};

} // namespace kb::editor
