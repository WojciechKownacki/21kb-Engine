#include "rendering/ParticleEditorPanelLayout.hpp"

#if defined(_WIN32)
#include "rendering/components/CategoryHeader.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] int Scale(int value, unsigned int dpi) noexcept {
    return std::max(1, MulDiv(value, static_cast<int>(dpi), 96));
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool Empty(const RECT& rect) noexcept {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

[[nodiscard]] int PropertyHeight(const kb::particle_editor::ParticleEditorPropertyRow& row, unsigned int dpi) noexcept {
    using kb::particle_editor::ParticleEditorPropertyWidget;
    switch (row.widget) {
    case ParticleEditorPropertyWidget::Slider:
    case ParticleEditorPropertyWidget::IntegerSlider:
        return Scale(38, dpi);
    case ParticleEditorPropertyWidget::Vector:
        return Scale(32, dpi);
    case ParticleEditorPropertyWidget::Enum: {
        const int chipRows = row.enumCount <= 3U ? 1 : (row.enumCount <= 6U ? 2 : 3);
        return Scale(16, dpi) + chipRows * Scale(24, dpi) + Scale(6, dpi);
    }
    case ParticleEditorPropertyWidget::Curve:
    case ParticleEditorPropertyWidget::Gradient:
        return Scale(40, dpi);
    case ParticleEditorPropertyWidget::Color:
        return Scale(32, dpi);
    case ParticleEditorPropertyWidget::Toggle:
    case ParticleEditorPropertyWidget::Text:
        return Scale(28, dpi);
    }
    return Scale(28, dpi);
}

void PlaceSlider(
    RECT& track,
    RECT& fill,
    RECT& thumb,
    LONG left,
    LONG right,
    LONG top,
    int trackHeight,
    float value,
    float minimum,
    float maximum,
    unsigned int dpi) noexcept {
    track = {left, top, std::max(left, right), top + trackHeight};
    const float span = std::max(0.0001F, maximum - minimum);
    const float t = std::clamp((value - minimum) / span, 0.0F, 1.0F);
    const LONG trackWidth = std::max<LONG>(0, track.right - track.left);
    const LONG fillRight = track.left + static_cast<LONG>(t * static_cast<float>(trackWidth) + 0.5F);
    fill = {track.left, track.top, fillRight, track.bottom};
    const int thumbSize = Scale(12, dpi);
    thumb = {fillRight - thumbSize / 2, top + trackHeight / 2 - thumbSize / 2,
             fillRight + (thumbSize + 1) / 2, top + trackHeight / 2 - thumbSize / 2 + thumbSize};
}

void PlacePropertyRow(
    ParticleEditorPropertyRowLayout& layout,
    const kb::particle_editor::ParticleEditorPropertyRow& property,
    const RECT& bounds,
    unsigned int dpi) noexcept {
    layout = {};
    layout.bounds = bounds;
    const int pad = Scale(6, dpi);
    const int valueWidth = Scale(56, dpi);
    layout.label = {bounds.left + pad, bounds.top, bounds.left + pad + Scale(108, dpi), bounds.bottom};
    layout.valueBox = {std::max(layout.label.right + Scale(4, dpi), bounds.right - pad - valueWidth),
                       bounds.top + Scale(4, dpi), bounds.right - pad, bounds.bottom - Scale(4, dpi)};
    using kb::particle_editor::ParticleEditorPropertyWidget;
    if (property.widget == ParticleEditorPropertyWidget::Slider ||
        property.widget == ParticleEditorPropertyWidget::IntegerSlider) {
        layout.hasSlider = true;
        const int labelHeight = Scale(16, dpi);
        layout.label = {bounds.left + pad, bounds.top, bounds.right - pad - valueWidth - Scale(4, dpi),
                        bounds.top + labelHeight};
        layout.valueBox = {bounds.right - pad - valueWidth, bounds.top + Scale(1, dpi),
                           bounds.right - pad, bounds.top + labelHeight};
        const LONG trackHeight = Scale(8, dpi);
        PlaceSlider(layout.sliderTrack, layout.sliderFill, layout.sliderThumb,
            bounds.left + pad, bounds.right - pad, bounds.top + labelHeight + Scale(4, dpi),
            trackHeight, property.numericValue, property.numericMin, property.numericMax, dpi);
    } else if (property.widget == ParticleEditorPropertyWidget::Toggle) {
        layout.hasToggle = true;
        const int toggleWidth = Scale(36, dpi);
        const int toggleHeight = Scale(18, dpi);
        layout.toggle = {bounds.right - pad - toggleWidth, (bounds.top + bounds.bottom - toggleHeight) / 2,
                         bounds.right - pad, (bounds.top + bounds.bottom - toggleHeight) / 2 + toggleHeight};
        layout.valueBox = {};
        layout.label.right = layout.toggle.left - Scale(8, dpi);
    } else if (property.widget == ParticleEditorPropertyWidget::Enum) {
        layout.enumChipCount = std::min<std::uint8_t>(property.enumCount, static_cast<std::uint8_t>(layout.enumChips.size()));
        const int labelHeight = Scale(16, dpi);
        layout.label = {bounds.left + pad, bounds.top, bounds.right - pad, bounds.top + labelHeight};
        layout.valueBox = {};
        const LONG chipLeft = bounds.left + pad;
        const LONG chipRight = bounds.right - pad;
        const LONG chipAreaWidth = std::max<LONG>(0, chipRight - chipLeft);
        const LONG chipGap = Scale(4, dpi);
        const LONG minChipWidth = Scale(92, dpi);
        const LONG chipHeight = Scale(22, dpi);
        const int chipsPerRow = std::max(1, std::min<int>(layout.enumChipCount,
            static_cast<int>((chipAreaWidth + chipGap) / (minChipWidth + chipGap))));
        const int columns = std::max(1, chipsPerRow);
        const LONG chipWidth = columns == 0
            ? 0
            : std::max(minChipWidth,
                  (chipAreaWidth - chipGap * std::max(0, columns - 1)) / columns);
        for (std::uint8_t index = 0U; index < layout.enumChipCount; ++index) {
            const int column = static_cast<int>(index) % columns;
            const int row = static_cast<int>(index) / columns;
            const LONG left = chipLeft + static_cast<LONG>(column) * (chipWidth + chipGap);
            const LONG top = bounds.top + labelHeight + Scale(2, dpi) + static_cast<LONG>(row) * (chipHeight + Scale(2, dpi));
            layout.enumChips[index] = {left, top, left + chipWidth, top + chipHeight};
        }
    } else if (property.widget == ParticleEditorPropertyWidget::Vector) {
        const LONG axisAreaLeft = layout.label.right + Scale(6, dpi);
        const LONG axisGap = Scale(4, dpi);
        const LONG axisWidth = std::max<LONG>(Scale(42, dpi),
            (bounds.right - pad - axisAreaLeft - axisGap * 2) / 3);
        for (int axis = 0; axis < 3; ++axis) {
            const LONG left = axisAreaLeft + static_cast<LONG>(axis) * (axisWidth + axisGap);
            layout.vectorAxes[static_cast<std::size_t>(axis)] = {
                left, bounds.top + Scale(4, dpi), left + axisWidth, bounds.bottom - Scale(4, dpi)};
        }
        layout.valueBox = {};
    } else if (property.widget == ParticleEditorPropertyWidget::Curve ||
               property.widget == ParticleEditorPropertyWidget::Gradient) {
        layout.valueBox = {};
        layout.curvePreview = {layout.label.right + Scale(6, dpi), bounds.top + Scale(6, dpi),
                               bounds.right - pad, bounds.bottom - Scale(6, dpi)};
    } else if (property.widget == ParticleEditorPropertyWidget::Color) {
        layout.valueBox = {};
        layout.colorSwatch = {layout.label.right + Scale(6, dpi), bounds.top + Scale(4, dpi),
                              bounds.right - pad, bounds.bottom - Scale(4, dpi)};
    }
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
    const int toolbarHeight = Scale(40, dpi);
    const int statusHeight = Scale(22, dpi);
    const int minimumComposerWidth = Scale(300, dpi);
    const int composerWidth = std::min(width, std::max(minimumComposerWidth, width / 3));
    const LONG bodyTop = std::min(content.bottom, content.top + toolbarHeight);
    const LONG bodyBottom = std::max(bodyTop, content.bottom - statusHeight);
    const LONG composerLeft = std::max(content.left, content.right - composerWidth);
    const int padding = Scale(8, dpi);
    const int composerHeaderHeight = Scale(CategoryHeader::PreferredHeight, dpi);
    const int sectionHeaderHeight = Scale(CategoryHeader::PreferredHeight, dpi);
    const int addHeight = Scale(30, dpi);
    const int rowHeight = Scale(32, dpi);
    const int recipeTileHeight = Scale(56, dpi);
    const int outputChoiceHeight = Scale(30, dpi);
    const LONG scrollTop = bodyTop;
    const LONG scrollBottom = bodyBottom;
    const LONG scrollTrackWidth = Scale(8, dpi);

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
    const int horizontalGap = Scale(4, dpi);
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
        const RECT row{rowsLeft, rowTop, rowsRight, rowTop + rowHeight - Scale(3, dpi)};
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
    layout.propertyRowCount = inspector == nullptr
        ? 0U : std::min(inspector->properties.size(), layout.propertyRows.size());
    const auto placeIndexedProperty = [&](std::size_t index) noexcept {
        if (index >= layout.propertyRowCount) return;
        const auto& property = inspector->properties[index];
        const int height = PropertyHeight(property, dpi);
        const RECT bounds{rowsLeft, streamTop, rowsRight, streamTop + height - Scale(2, dpi)};
        PlacePropertyRow(layout.propertyRows[index], property, bounds, dpi);
        streamTop += height;
    };
    if (settingsExpanded && inspector != nullptr) {
        for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
            if (kb::particle_editor::IsParticleEditorSpawnProperty(inspector->properties[index].property) &&
                inspector->properties[index].property != kb::particle_editor::ParticleEditorProperty::ModulePayload)
                placeIndexedProperty(index);
        }
    }
    streamTop += padding;
    layout.recipeHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    layout.recipeTileCount = recipesExpanded ? std::min(recipeCount, layout.recipeTiles.size()) : 0U;
    const LONG recipeTileGap = Scale(6, dpi);
    const LONG recipeTileWidth = std::max<LONG>(0, (rowsRight - rowsLeft - recipeTileGap) / 2);
    for (std::size_t index = 0U; index < layout.recipeTileCount; ++index) {
        const std::size_t column = index % 2U;
        const std::size_t row = index / 2U;
        const LONG left = rowsLeft + static_cast<LONG>(column) * (recipeTileWidth + recipeTileGap);
        const LONG top = streamTop + static_cast<LONG>(row * static_cast<std::size_t>(recipeTileHeight));
        layout.recipeTiles[index] = {left, top, column == 0U ? left + recipeTileWidth : rowsRight,
            top + recipeTileHeight - Scale(4, dpi)};
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
        const RECT row{rowsLeft, streamTop, rowsRight, streamTop + rowHeight - Scale(3, dpi)};
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
        if (inspector->modules[index].selected) {
            for (std::size_t propertyIndex = 0U; propertyIndex < layout.propertyRowCount; ++propertyIndex) {
                if (inspector->properties[propertyIndex].property ==
                        kb::particle_editor::ParticleEditorProperty::ModulePayload &&
                    inspector->properties[propertyIndex].moduleId == inspector->modules[index].moduleId)
                    placeIndexedProperty(propertyIndex);
            }
        }
    }
    streamTop += padding;
    layout.outputHeader = {rowsLeft, streamTop, rowsRight, streamTop + sectionHeaderHeight};
    streamTop += sectionHeaderHeight;
    if (outputExpanded) {
        const LONG assetPickerGap = Scale(4, dpi);
        const LONG assetPickerWidth = std::max<LONG>(0, (rowsRight - rowsLeft - assetPickerGap * 2) / 3);
        layout.materialPicker = {rowsLeft, streamTop, rowsLeft + assetPickerWidth, streamTop + Scale(28, dpi)};
        layout.meshPicker = {layout.materialPicker.right + assetPickerGap, streamTop,
            layout.materialPicker.right + assetPickerGap + assetPickerWidth, streamTop + Scale(28, dpi)};
        layout.texturePicker = {layout.meshPicker.right + assetPickerGap, streamTop, rowsRight, streamTop + Scale(28, dpi)};
        streamTop += Scale(28, dpi) + padding;
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
        if (inspector != nullptr) {
            for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
                if (kb::particle_editor::IsParticleEditorOutputProperty(inspector->properties[index].property))
                    placeIndexedProperty(index);
            }
        }
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
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
        const auto& row = layout.propertyRows[index];
        if (Empty(row.bounds) || !Contains(row.bounds, x, y)) continue;
        if (row.hasSlider) {
            if (!Empty(row.valueBox) && Contains(row.valueBox, x, y))
                return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
            return {.action = ParticleEditorPanelAction::DragPropertySlider, .propertyIndex = index};
        }
        if (row.hasToggle && Contains(row.toggle, x, y))
            return {.action = ParticleEditorPanelAction::ToggleProperty, .propertyIndex = index};
        for (std::uint8_t chip = 0U; chip < row.enumChipCount; ++chip) {
            if (Contains(row.enumChips[chip], x, y))
                return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index,
                        .propertyChoice = chip};
        }
        if (!Empty(row.valueBox) && Contains(row.valueBox, x, y))
            return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
        if (!Empty(row.curvePreview) && Contains(row.curvePreview, x, y))
            return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
        if (!Empty(row.colorSwatch) && Contains(row.colorSwatch, x, y))
            return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
        for (const RECT& axis : row.vectorAxes) {
            if (!Empty(axis) && Contains(axis, x, y))
                return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
        }
        return {.action = ParticleEditorPanelAction::EditProperty, .propertyIndex = index};
    }
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

float ParticleEditorPanelLayoutResolver::SliderValueAt(
    const RECT& track, int x, float minimum, float maximum) noexcept {
    const float width = static_cast<float>(track.right - track.left);
    if (width <= 0.0F) return minimum;
    const float t = std::clamp(static_cast<float>(x - track.left) / width, 0.0F, 1.0F);
    return minimum + t * (maximum - minimum);
}

} // namespace kb::editor
#endif
