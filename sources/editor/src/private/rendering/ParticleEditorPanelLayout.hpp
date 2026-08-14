#pragma once

#include "editor/ParticleEmitterListModel.hpp"

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
};

#if defined(_WIN32)
struct ParticleEditorEmitterRowLayout {
    kb::scene::ParticleStableId emitterId = 0U;
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
};

struct ParticleEditorPanelHit {
    ParticleEditorPanelAction action = ParticleEditorPanelAction::None;
    kb::scene::ParticleStableId emitterId = 0U;
    std::uint32_t authoringOrder = 0U;
};

class ParticleEditorPanelLayoutResolver final {
public:
    ParticleEditorPanelLayoutResolver() = delete;
    [[nodiscard]] static ParticleEditorPanelLayout Resolve(
        const RECT& content,
        std::span<const kb::particle_editor::ParticleEmitterListRow> emitters,
        int composerScrollOffset,
        unsigned int dpi = 96U) noexcept;
    [[nodiscard]] static ParticleEditorPanelHit HitTest(
        const ParticleEditorPanelLayout& layout, int x, int y) noexcept;
    [[nodiscard]] static std::uint32_t ReorderTargetAt(
        const ParticleEditorPanelLayout& layout, int y) noexcept;
    [[nodiscard]] static int MaximumComposerScroll(
        const ParticleEditorPanelLayout& layout, unsigned int dpi = 96U) noexcept;
};
#endif

} // namespace kb::editor
