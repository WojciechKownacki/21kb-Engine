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
    const kb::particle_editor::ParticleEmitterInspectorView* inspector,
    std::size_t recipeCount,
    const kb::particle_editor::ParticleEditorWorkspaceState* workspace) noexcept {
    const int width = static_cast<int>(std::max<LONG>(0, content.right - content.left));
    const int toolbarHeight = Scale(32, dpi);
    const int statusHeight = Scale(20, dpi);
    const int minimumComposerWidth = Scale(280, dpi);
    const int composerWidth = std::min(width, std::max(minimumComposerWidth, width / 3));
    const LONG bodyTop = std::min(content.bottom, content.top + toolbarHeight);
    const LONG bodyBottom = std::max(bodyTop, content.bottom - statusHeight);
    const LONG composerLeft = std::max(content.left, content.right - composerWidth);
    const int padding = Scale(6, dpi);
    const int composerHeaderHeight = Scale(28, dpi);
    const int sectionHeaderHeight = Scale(24, dpi);
    const int addHeight = Scale(28, dpi);
    const int rowHeight = Scale(30, dpi);
    const int compactRowHeight = Scale(28, dpi);
    const int recipeTileHeight = Scale(48, dpi);
    const int outputChoiceHeight = Scale(28, dpi);
    const LONG scrollTop = bodyTop;
    const LONG scrollBottom = bodyBottom;
    const LONG scrollTrackWidth = Scale(10, dpi);

    ParticleEditorPanelLayout layout{
        .toolbar = {content.left, content.top, content.right, bodyTop},
        .preview = {content.left, bodyTop, composerLeft, bodyBottom},
        .composer = {composerLeft, bodyTop, content.right, bodyBottom},
        .composerScrollTrack = {content.right - padding - scrollTrackWidth, scrollTop,
                                content.right - padding, scrollBottom},
        .emitterList = {composerLeft, scrollTop, content.right, scrollBottom},
        .statusBar = {content.left, bodyBottom, content.right, content.bottom},
    };
    const int iconWidth = Scale(22, dpi);
    const int gripWidth = Scale(14, dpi);
    const int horizontalGap = Scale(3, dpi);
    const LONG rowsLeft = composerLeft + padding;
    const LONG rowsRight = std::max(rowsLeft, layout.composerScrollTrack.left - padding);
    const LONG initialStreamTop = scrollTop + padding;
    LONG streamTop = initialStreamTop - std::max(0, composerScrollOffset);
    const auto expanded = [workspace](kb::particle_editor::ParticleEditorComposerSection section) noexcept {
        return workspace == nullptr || workspace->ComposerSectionExpanded(section);
    };
    const bool emittersExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Emitters);
    const bool settingsExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Settings);
    const bool recipesExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Recipes);
    const bool modulesExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Modules);
    const bool outputExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Output);
    const bool dependenciesExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Dependencies);
    const bool diagnosticsExpanded = expanded(kb::particle_editor::ParticleEditorComposerSection::Diagnostics);
    layout.composerHeader = {rowsLeft, streamTop, rowsRight, streamTop + composerHeaderHeight};
    streamTop += composerHeaderHeight + padding;
    if (emittersExpanded) {
        layout.addEmitter = {rowsLeft, streamTop, rowsRight, streamTop + addHeight};
        streamTop += addHeight + padding;
    }
    const LONG firstRowTop = streamTop;
    layout.emitterRowCount = emittersExpanded ? std::min(emitters.size(), layout.emitterRows.size()) : 0U;
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
    streamTop = firstRowTop + static_cast<LONG>(layout.emitterRowCount * static_cast<std::size_t>(rowHeight)) + padding;
    layout.propertyHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    layout.propertyRowCount = settingsExpanded && inspector != nullptr
        ? std::min(inspector->properties.size(), layout.propertyRows.size()) : 0U;
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
        layout.propertyRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + compactRowHeight - Scale(1, dpi)};
        streamTop += compactRowHeight;
    }
    streamTop += padding;
    layout.recipeHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    layout.recipeTileCount = recipesExpanded ? std::min(recipeCount, layout.recipeTiles.size()) : 0U;
    const LONG recipeTileGap = Scale(4, dpi);
    const LONG recipeTileWidth = std::max<LONG>(0, (rowsRight - rowsLeft - recipeTileGap) / 2);
    for (std::size_t index = 0U; index < layout.recipeTileCount; ++index) {
        const std::size_t column = index % 2U;
        const std::size_t row = index / 2U;
        const LONG left = rowsLeft + static_cast<LONG>(column) * (recipeTileWidth + recipeTileGap);
        const LONG top = streamTop + static_cast<LONG>(row * static_cast<std::size_t>(recipeTileHeight));
        layout.recipeTiles[index] = {left, top, column == 0U ? left + recipeTileWidth : rowsRight,
            top + recipeTileHeight - Scale(3, dpi)};
    }
    streamTop += static_cast<LONG>((layout.recipeTileCount + 1U) / 2U) * recipeTileHeight + padding;
    layout.moduleHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    if (modulesExpanded) {
        layout.addModule = {rowsLeft, streamTop, rowsRight, streamTop + addHeight};
        streamTop += addHeight + padding;
    }
    layout.moduleRowCount = modulesExpanded && inspector != nullptr
        ? std::min(inspector->modules.size(), layout.moduleRows.size()) : 0U;
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
    streamTop += padding;
    layout.outputHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    if (outputExpanded) {
    const LONG assetPickerGap = Scale(4, dpi);
    const LONG assetPickerWidth = std::max<LONG>(0, (rowsRight - rowsLeft - assetPickerGap * 2) / 3);
    layout.materialPicker = {rowsLeft, streamTop, rowsLeft + assetPickerWidth, streamTop + compactRowHeight};
    layout.meshPicker = {layout.materialPicker.right + assetPickerGap, streamTop,
        layout.materialPicker.right + assetPickerGap + assetPickerWidth, streamTop + compactRowHeight};
    layout.texturePicker = {layout.meshPicker.right + assetPickerGap, streamTop, rowsRight, streamTop + compactRowHeight};
    streamTop += compactRowHeight + padding;
    layout.outputChoiceCount = inspector == nullptr ? 0U : std::min(inspector->outputChoices.size(), layout.outputChoices.size());
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index) {
        const std::size_t column = index % 2U;
        const std::size_t row = index / 2U;
        const LONG choiceGap = Scale(4, dpi);
        const LONG choiceWidth = std::max<LONG>(0, (rowsRight - rowsLeft - choiceGap) / 2);
        const LONG choiceLeft = rowsLeft + static_cast<LONG>(column) * (choiceWidth + choiceGap);
        const LONG choiceTop = streamTop + static_cast<LONG>(row * static_cast<std::size_t>(outputChoiceHeight));
        layout.outputChoices[index] = {choiceLeft, choiceTop,
            column == 0U ? choiceLeft + choiceWidth : rowsRight, choiceTop + outputChoiceHeight - Scale(2, dpi)};
    }
    streamTop += static_cast<LONG>((layout.outputChoiceCount + 1U) / 2U) * outputChoiceHeight + padding;
    }
    layout.dependencyHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight}; streamTop += sectionHeaderHeight;
    layout.dependencyRowCount = dependenciesExpanded && inspector != nullptr ?
        std::min(inspector->dependencies.size(), layout.dependencyRows.size()) : 0U;
    for (std::size_t index = 0U; index < layout.dependencyRowCount; ++index) {
        layout.dependencyRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    layout.diagnosticHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight}; streamTop += sectionHeaderHeight;
    layout.diagnosticRowCount = diagnosticsExpanded && inspector != nullptr ?
        std::min(inspector->diagnostics.size(), layout.diagnosticRows.size()) : 0U;
    for (std::size_t index = 0U; index < layout.diagnosticRowCount; ++index) {
        layout.diagnosticRows[index] = {rowsLeft, streamTop, rowsRight, streamTop + rowHeight};
        streamTop += rowHeight;
    }
    layout.composerContentHeight = static_cast<int>(std::max<LONG>(0,
        streamTop + std::max(0, composerScrollOffset) - initialStreamTop));
    const int visibleHeight = static_cast<int>(std::max<LONG>(0, scrollBottom - scrollTop));
    const int maximumScroll = std::max(0, layout.composerContentHeight - visibleHeight);
    if (maximumScroll > 0 && visibleHeight > 0) {
        const int trackHeight = std::max(1, static_cast<int>(layout.composerScrollTrack.bottom - layout.composerScrollTrack.top));
        const int thumbHeight = std::clamp(
            (visibleHeight * visibleHeight) / std::max(1, layout.composerContentHeight),
            Scale(24, dpi), trackHeight);
        const int clampedOffset = std::clamp(composerScrollOffset, 0, maximumScroll);
        const int thumbRange = std::max(0, trackHeight - thumbHeight);
        const int thumbTop = layout.composerScrollTrack.top +
            (thumbRange * clampedOffset) / maximumScroll;
        layout.composerScrollThumb = {layout.composerScrollTrack.left, thumbTop,
            layout.composerScrollTrack.right, thumbTop + thumbHeight};
    }
    return layout;
}

ParticleEditorPanelHit ParticleEditorPanelLayoutResolver::HitTest(
    const ParticleEditorPanelLayout& layout, int x, int y) noexcept {
    const auto sectionHit = [x, y](const RECT& rect, kb::particle_editor::ParticleEditorComposerSection section) {
        return Contains(rect, x, y)
            ? ParticleEditorPanelHit{.action = ParticleEditorPanelAction::ToggleComposerSection,
                                     .composerSection = section}
            : ParticleEditorPanelHit{};
    };
    if (!Contains(layout.emitterList, x, y)) return {};
    if (const auto hit = sectionHit(layout.composerHeader, kb::particle_editor::ParticleEditorComposerSection::Emitters);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.propertyHeader, kb::particle_editor::ParticleEditorComposerSection::Settings);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.recipeHeader, kb::particle_editor::ParticleEditorComposerSection::Recipes);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.moduleHeader, kb::particle_editor::ParticleEditorComposerSection::Modules);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.outputHeader, kb::particle_editor::ParticleEditorComposerSection::Output);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.dependencyHeader, kb::particle_editor::ParticleEditorComposerSection::Dependencies);
        hit.action != ParticleEditorPanelAction::None) return hit;
    if (const auto hit = sectionHit(layout.diagnosticHeader, kb::particle_editor::ParticleEditorComposerSection::Diagnostics);
        hit.action != ParticleEditorPanelAction::None) return hit;
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
    if (Contains(layout.materialPicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputMaterial};
    if (Contains(layout.meshPicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputMesh};
    if (Contains(layout.texturePicker, x, y)) return {.action = ParticleEditorPanelAction::PickOutputTexture};
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index)
        if (Contains(layout.outputChoices[index], x, y))
            return {.action = ParticleEditorPanelAction::SelectOutputType,
                    .outputType = static_cast<kb::scene::ParticleOutputType>(index)};
    for (std::size_t index = 0U; index < layout.recipeTileCount; ++index)
        if (Contains(layout.recipeTiles[index], x, y))
            return {.action = ParticleEditorPanelAction::AppendRecipe, .recipeIndex = index};
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
    static_cast<void>(dpi);
    const int visibleHeight = static_cast<int>(
        std::max<LONG>(0, layout.emitterList.bottom - layout.emitterList.top));
    return std::max(0, layout.composerContentHeight - visibleHeight);
}

} // namespace kb::editor
#endif
