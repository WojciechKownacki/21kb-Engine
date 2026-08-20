#include "rendering/ParticleEditorPanelLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] int Scale(int value, unsigned int dpi) noexcept {
    return std::max(1, MulDiv(value, static_cast<int>(dpi), 96));
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

ParticleEditorPanelLayout ParticleEditorPanelLayoutResolver::Resolve(
    const RECT& content,
    std::span<const kb::particle_editor::ParticleEmitterListRow> emitters,
    int composerScrollOffset,
    unsigned int dpi,
    const kb::particle_editor::ParticleEmitterInspectorView* inspector) noexcept {
    const int width = static_cast<int>(std::max<LONG>(0, content.right - content.left));
    const int toolbarHeight = Scale(32, dpi);
    const int statusHeight = Scale(20, dpi);
    const int minimumComposerWidth = Scale(280, dpi);
    const int composerWidth = std::min(width, std::max(minimumComposerWidth, width / 3));
    const LONG bodyTop = std::min(content.bottom, content.top + toolbarHeight);
    const LONG bodyBottom = std::max(bodyTop, content.bottom - statusHeight);
    const LONG composerLeft = std::max(content.left, content.right - composerWidth);
    const int padding = Scale(6, dpi);
    const int composerHeaderHeight = Scale(30, dpi);
    const int addHeight = Scale(24, dpi);
    const int rowHeight = Scale(30, dpi);
    const LONG headerBottom = std::min(bodyBottom, bodyTop + composerHeaderHeight);
    const LONG addTop = std::min(bodyBottom, headerBottom + padding);
    const LONG addBottom = std::min(bodyBottom, addTop + addHeight);
    const LONG listTop = std::min(bodyBottom, addBottom + padding);

    ParticleEditorPanelLayout layout{
        .toolbar = {content.left, content.top, content.right, bodyTop},
        .preview = {content.left, bodyTop, composerLeft, bodyBottom},
        .composer = {composerLeft, bodyTop, content.right, bodyBottom},
        .composerHeader = {composerLeft, bodyTop, content.right, headerBottom},
        .addEmitter = {composerLeft + padding, addTop, content.right - padding, addBottom},
        .emitterList = {composerLeft, listTop, content.right, bodyBottom},
        .statusBar = {content.left, bodyBottom, content.right, content.bottom},
    };
    const int iconWidth = Scale(18, dpi);
    const int gripWidth = Scale(14, dpi);
    const int horizontalGap = Scale(3, dpi);
    const LONG rowsLeft = composerLeft + padding;
    const LONG rowsRight = content.right - padding;
    const LONG firstRowTop = listTop - std::max(0, composerScrollOffset);
    layout.emitterRowCount = std::min(emitters.size(), layout.emitterRows.size());
    for (std::size_t index = 0U; index < layout.emitterRowCount; ++index) {
        const LONG rowTop = firstRowTop + static_cast<LONG>(index * static_cast<std::size_t>(rowHeight));
        const RECT row{rowsLeft, rowTop, rowsRight, rowTop + rowHeight - Scale(2, dpi)};
        LONG actionRight = rowsRight - horizontalGap;
        const auto takeAction = [&](LONG& right) noexcept {
            const RECT result{right - iconWidth, row.top + horizontalGap, right,
                              row.bottom - horizontalGap};
            right = result.left - horizontalGap;
            return result;
        };
        const RECT remove = takeAction(actionRight);
        const RECT down = takeAction(actionRight);
        const RECT up = takeAction(actionRight);
        const RECT enabled = takeAction(actionRight);
        layout.emitterRows[index] = {
            .emitterId = emitters[index].emitterId,
            .authoringOrder = emitters[index].authoringOrder,
            .bounds = row,
            .dragGrip = {row.left + horizontalGap, row.top + horizontalGap,
                         row.left + horizontalGap + gripWidth, row.bottom - horizontalGap},
            .enabledToggle = enabled,
            .name = {row.left + horizontalGap * 2 + gripWidth, row.top, actionRight, row.bottom},
            .moveUp = up,
            .moveDown = down,
            .remove = remove,
        };
    }
    LONG streamTop = firstRowTop + static_cast<LONG>(layout.emitterRowCount * static_cast<std::size_t>(rowHeight)) + padding;
    layout.outputHeader = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
    streamTop += rowHeight;
    layout.materialPicker = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.meshPicker = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.texturePicker = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.outputChoiceCount = inspector == nullptr ? 0U : std::min(inspector->outputChoices.size(), layout.outputChoices.size());
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index) {
        layout.outputChoices[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    layout.propertyRowCount = inspector == nullptr ? 0U : std::min(inspector->properties.size(), layout.propertyRows.size());
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
        layout.propertyRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    layout.moduleHeader = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.addModule = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.moduleRowCount = inspector == nullptr ? 0U : std::min(inspector->modules.size(), layout.moduleRows.size());
    for (std::size_t index = 0U; index < layout.moduleRowCount; ++index) {
        const RECT row{rowsLeft, streamTop, rowsRight, streamTop + rowHeight - Scale(2, dpi)};
        streamTop += rowHeight;
        LONG actionRight = rowsRight - horizontalGap;
        const auto takeAction = [&](LONG& right) noexcept {
            const RECT result{right - iconWidth, row.top + horizontalGap, right, row.bottom - horizontalGap};
            right = result.left - horizontalGap;
            return result;
        };
        const RECT remove = takeAction(actionRight);
        const RECT down = takeAction(actionRight);
        const RECT up = takeAction(actionRight);
        const RECT enabled = takeAction(actionRight);
        layout.moduleRows[index] = {.emitterId = inspector->emitterId,
            .moduleId = inspector->modules[index].moduleId,
            .authoringOrder = inspector->modules[index].authoringOrder, .bounds = row,
            .dragGrip = {row.left + horizontalGap, row.top + horizontalGap,
                         row.left + horizontalGap + gripWidth, row.bottom - horizontalGap},
            .enabledToggle = enabled,
            .name = {row.left + horizontalGap * 2 + gripWidth, row.top, actionRight, row.bottom},
            .moveUp = up, .moveDown = down, .remove = remove};
    }
    layout.dependencyHeader = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.dependencyRowCount = inspector == nullptr ? 0U :
        std::min(inspector->dependencies.size(), layout.dependencyRows.size());
    for (std::size_t index = 0U; index < layout.dependencyRowCount; ++index) {
        layout.dependencyRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    layout.diagnosticHeader = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight}; streamTop += rowHeight;
    layout.diagnosticRowCount = inspector == nullptr ? 0U : std::min(inspector->diagnostics.size(), layout.diagnosticRows.size());
    for (std::size_t index = 0U; index < layout.diagnosticRowCount; ++index) {
        layout.diagnosticRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    return layout;
}

ParticleEditorPanelHit ParticleEditorPanelLayoutResolver::HitTest(
    const ParticleEditorPanelLayout& layout, int x, int y) noexcept {
    if (Contains(layout.addEmitter, x, y) &&
        layout.emitterRowCount < kb::scene::kParticleEffectMaxEmitters)
        return {.action = ParticleEditorPanelAction::AddEmitter};
    for (std::size_t index = 0U; index < layout.emitterRowCount; ++index) {
        const ParticleEditorEmitterRowLayout& row = layout.emitterRows[index];
        if (!Contains(layout.emitterList, x, y) || !Contains(row.bounds, x, y))
            continue;
        ParticleEditorPanelAction action = ParticleEditorPanelAction::SelectEmitter;
        if (Contains(row.dragGrip, x, y)) action = ParticleEditorPanelAction::BeginEmitterDrag;
        else if (Contains(row.enabledToggle, x, y)) action = ParticleEditorPanelAction::ToggleEmitter;
        else if (Contains(row.moveUp, x, y)) action = ParticleEditorPanelAction::MoveEmitterUp;
        else if (Contains(row.moveDown, x, y)) action = ParticleEditorPanelAction::MoveEmitterDown;
        else if (Contains(row.remove, x, y)) action = ParticleEditorPanelAction::RemoveEmitter;
        return {.action = action, .emitterId = row.emitterId, .authoringOrder = row.authoringOrder};
    }
    if (!Contains(layout.emitterList, x, y)) return {};
    if (Contains(layout.materialPicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputMaterial};
    if (Contains(layout.meshPicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputMesh};
    if (Contains(layout.texturePicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputTexture};
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index)
        if (Contains(layout.outputChoices[index], x, y))
            return {.action = ParticleEditorPanelAction::SelectOutputType,
                    .outputType = static_cast<kb::scene::ParticleOutputType>(index)};
    if (Contains(layout.addModule, x, y)) return {.action = ParticleEditorPanelAction::AddModule};
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index)
        if (Contains(layout.propertyRows[index], x, y))
            return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
    for (std::size_t index = 0U; index < layout.moduleRowCount; ++index) {
        const auto& row = layout.moduleRows[index];
        if (!Contains(row.bounds, x, y)) continue;
        auto action = ParticleEditorPanelAction::SelectModule;
        if (Contains(row.dragGrip, x, y)) action = ParticleEditorPanelAction::BeginModuleDrag;
        else if (Contains(row.enabledToggle, x, y)) action = ParticleEditorPanelAction::ToggleModule;
        else if (Contains(row.moveUp, x, y)) action = ParticleEditorPanelAction::MoveModuleUp;
        else if (Contains(row.moveDown, x, y)) action = ParticleEditorPanelAction::MoveModuleDown;
        else if (Contains(row.remove, x, y)) action = ParticleEditorPanelAction::RemoveModule;
        return {.action = action, .emitterId = row.emitterId, .authoringOrder = row.authoringOrder,
                .moduleId = row.moduleId};
    }
    for (std::size_t index = 0U; index < layout.diagnosticRowCount; ++index)
        if (Contains(layout.diagnosticRows[index], x, y))
            return {.action = ParticleEditorPanelAction::NavigateDiagnostic, .diagnosticIndex = index};
    for (std::size_t index = 0U; index < layout.dependencyRowCount; ++index)
        if (Contains(layout.dependencyRows[index], x, y))
            return {.action = ParticleEditorPanelAction::NavigateDependency, .dependencyIndex = index};
    return {};
}

std::uint32_t ParticleEditorPanelLayoutResolver::ReorderTargetAt(
    const ParticleEditorPanelLayout& layout, int y) noexcept {
    if (layout.emitterRowCount == 0U)
        return 0U;
    for (std::size_t index = 0U; index < layout.emitterRowCount; ++index) {
        const auto& row = layout.emitterRows[index];
        if (y < (row.bounds.top + row.bounds.bottom) / 2)
            return row.authoringOrder;
    }
    return layout.emitterRows[layout.emitterRowCount - 1U].authoringOrder;
}

std::uint32_t ParticleEditorPanelLayoutResolver::ModuleReorderTargetAt(
    const ParticleEditorPanelLayout& layout, int y) noexcept {
    if (layout.moduleRowCount == 0U) return 0U;
    for (std::size_t index = 0U; index < layout.moduleRowCount; ++index) {
        const auto& row = layout.moduleRows[index];
        if (y < (row.bounds.top + row.bounds.bottom) / 2) return row.authoringOrder;
    }
    return layout.moduleRows[layout.moduleRowCount - 1U].authoringOrder;
}

int ParticleEditorPanelLayoutResolver::MaximumComposerScroll(
    const ParticleEditorPanelLayout& layout, unsigned int dpi) noexcept {
    int contentHeight = static_cast<int>(layout.emitterRowCount + layout.outputChoiceCount +
        layout.propertyRowCount + layout.moduleRowCount + layout.dependencyRowCount +
        layout.diagnosticRowCount + 8U) * Scale(30, dpi) + Scale(6, dpi);
    const int visibleHeight = static_cast<int>(
        std::max<LONG>(0, layout.emitterList.bottom - layout.emitterList.top));
    return std::max(0, contentHeight - visibleHeight);
}

} // namespace kb::editor
#endif
