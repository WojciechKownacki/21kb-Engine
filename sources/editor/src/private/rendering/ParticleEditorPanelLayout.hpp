#pragma once

#include "editor/ParticleEmitterListModel.hpp"
#include "editor/ParticleEmitterInspectorModel.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstdint>
#include <span>

namespace kb::editor {

enum class ParticleEditorPanelAction : std::uint8_t {
    None,
    AddEmitter,
    SelectEmitter,
    ToggleEmitter,
    MoveEmitterUp,
    MoveEmitterDown,
    RemoveEmitter,
    BeginEmitterDrag,
    SelectOutputType,
    PickOutputMaterial,
    PickOutputMesh,
    PickOutputTexture,
    EditProperty,
    AddModule,
    SelectModule,
    BeginModuleDrag,
    ToggleModule,
    MoveModuleUp,
    MoveModuleDown,
    RemoveModule,
    NavigateDiagnostic,
    NavigateDependency,
};

#if defined(_WIN32)
struct ParticleEditorEmitterRowLayout {
    kb::scene::ParticleStableId emitterId = 0U;
    kb::scene::ParticleStableId moduleId = 0U;
    std::uint32_t authoringOrder = 0U;
    RECT bounds{};
    RECT dragGrip{};
    RECT enabledToggle{};
    RECT name{};
    RECT moveUp{};
    RECT moveDown{};
    RECT remove{};
};

struct ParticleEditorPanelLayout {
    RECT toolbar{};
    RECT preview{};
    RECT composer{};
    RECT composerHeader{};
    RECT addEmitter{};
    RECT emitterList{};
    RECT statusBar{};
    std::array<ParticleEditorEmitterRowLayout, kb::scene::kParticleEffectMaxEmitters> emitterRows{};
    std::size_t emitterRowCount = 0U;
    RECT outputHeader{};
    RECT materialPicker{};
    RECT meshPicker{};
    RECT texturePicker{};
    std::array<RECT, 8U> outputChoices{};
    std::size_t outputChoiceCount = 0U;
    std::array<RECT, 128U> propertyRows{};
    std::size_t propertyRowCount = 0U;
    RECT moduleHeader{};
    RECT addModule{};
    std::array<ParticleEditorEmitterRowLayout, kb::scene::kParticleEffectMaxModulesPerEmitter> moduleRows{};
    std::size_t moduleRowCount = 0U;
    RECT dependencyHeader{};
    std::array<RECT, kb::scene::kParticleEffectMaxDependencyAssets> dependencyRows{};
    std::size_t dependencyRowCount = 0U;
    RECT diagnosticHeader{};
    std::array<RECT, kb::scene::kParticleEffectMaxDiagnostics> diagnosticRows{};
    std::size_t diagnosticRowCount = 0U;
};

struct ParticleEditorPanelHit {
    ParticleEditorPanelAction action = ParticleEditorPanelAction::None;
    kb::scene::ParticleStableId emitterId = 0U;
    std::uint32_t authoringOrder = 0U;
    kb::scene::ParticleStableId moduleId = 0U;
    kb::scene::ParticleOutputType outputType = kb::scene::ParticleOutputType::Billboard;
    kb::scene::ParticleModuleType moduleType = kb::scene::ParticleModuleType::InitialVelocity;
    std::size_t diagnosticIndex = 0U;
    std::size_t dependencyIndex = 0U;
    std::size_t propertyIndex = 0U;
};

class ParticleEditorPanelLayoutResolver final {
public:
    ParticleEditorPanelLayoutResolver() = delete;
    [[nodiscard]] static ParticleEditorPanelLayout Resolve(
        const RECT& content,
        std::span<const kb::particle_editor::ParticleEmitterListRow> emitters,
        int composerScrollOffset,
        unsigned int dpi = 96U,
        const kb::particle_editor::ParticleEmitterInspectorView* inspector = nullptr) noexcept;
    [[nodiscard]] static ParticleEditorPanelHit HitTest(
        const ParticleEditorPanelLayout& layout, int x, int y) noexcept;
    [[nodiscard]] static std::uint32_t ReorderTargetAt(
        const ParticleEditorPanelLayout& layout, int y) noexcept;
    [[nodiscard]] static std::uint32_t ModuleReorderTargetAt(
        const ParticleEditorPanelLayout& layout, int y) noexcept;
    [[nodiscard]] static int MaximumComposerScroll(
        const ParticleEditorPanelLayout& layout, unsigned int dpi = 96U) noexcept;
};
#endif

} // namespace kb::editor
