#include "rendering/ParticleEditorPanelLayout.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::vector<kb::particle_editor::ParticleEmitterListRow> Rows(std::size_t count) {
    std::vector<kb::particle_editor::ParticleEmitterListRow> rows;
    rows.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        rows.push_back({.emitterId = 100U + index,
                        .authoringOrder = static_cast<std::uint32_t>(index),
                        .name = "Emitter " + std::to_string(index + 1U),
                        .enabled = (index % 2U) == 0U,
                        .selected = index == 3U});
    }
    return rows;
}

[[nodiscard]] POINT Center(const RECT& rect) noexcept {
    return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
}

void TestTwoToOneResponsiveGeometry() {
    const auto rows = Rows(8U);
    for (const auto fixture : {
             std::pair{RECT{0, 0, 900, 560}, 96U},
             std::pair{RECT{0, 0, 1280, 720}, 96U},
             std::pair{RECT{0, 0, 3840, 2160}, 192U},
         }) {
        const auto layout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
            kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
                fixture.first, rows, 0, fixture.second));
        const LONG previewWidth = layout->preview.right - layout->preview.left;
        const LONG composerWidth = layout->composer.right - layout->composer.left;
        Require(previewWidth >= composerWidth * 2 - 2 && previewWidth <= composerWidth * 2 + 2,
            "particle panel did not preserve the two-to-one preview/composer layout");
        Require(layout->toolbar.top == fixture.first.top && layout->statusBar.bottom == fixture.first.bottom &&
                layout->preview.bottom == layout->composer.bottom,
            "particle panel responsive bands did not fill the content rect");
    }
}

void TestCompactAuthoringMetrics() {
    const auto layout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(RECT{0, 0, 1280, 720}, Rows(2U), 0, 96U));
    Require(layout->toolbar.bottom - layout->toolbar.top == 40,
        "particle toolbar did not use the production height");
    Require(layout->statusBar.bottom - layout->statusBar.top == 22,
        "particle status bar did not use the production height");
    Require(layout->addEmitter.bottom - layout->addEmitter.top == 30,
        "particle primary action did not use a usable control height");
    Require(layout->emitterRows[0].bounds.bottom - layout->emitterRows[0].bounds.top == 29,
        "particle emitter row did not preserve production spacing");
    Require(layout->emitterRows[0].enabledToggle.right - layout->emitterRows[0].enabledToggle.left >= 20 &&
            layout->emitterRows[0].enabledToggle.bottom - layout->emitterRows[0].enabledToggle.top >= 20,
        "emitter action target became too small for reliable pointer use");
}

void TestHitTestingScrollAndSelectionFocus() {
    const auto rows = Rows(8U);
    const RECT content{0, 0, 900, 300};
    const auto unscrolled = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U));
    Require(kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(*unscrolled, 96U) > 0,
        "bounded emitter list did not expose scrolling in a short composer");
    const auto scrolled = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 72, 96U));
    Require(scrolled->emitterRows[0].bounds.top == unscrolled->emitterRows[0].bounds.top - 72,
        "composer scroll offset did not move emitter rows deterministically");

    const POINT add = Center(unscrolled->addEmitter);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*unscrolled, add.x, add.y).action ==
            kb::editor::ParticleEditorPanelAction::None,
        "full emitter list did not disable the add hit target");
    const auto availableRows = Rows(7U);
    const auto available = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, availableRows, 0, 96U));
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*available, add.x, add.y).action ==
            kb::editor::ParticleEditorPanelAction::AddEmitter,
        "available emitter slot did not route the add hit target");
    const auto& row = unscrolled->emitterRows[3];
    for (const auto fixture : {
             std::pair{row.name, kb::editor::ParticleEditorPanelAction::SelectEmitter},
             std::pair{row.dragGrip, kb::editor::ParticleEditorPanelAction::BeginEmitterDrag},
             std::pair{row.enabledToggle, kb::editor::ParticleEditorPanelAction::ToggleEmitter},
             std::pair{row.moveUp, kb::editor::ParticleEditorPanelAction::MoveEmitterUp},
             std::pair{row.moveDown, kb::editor::ParticleEditorPanelAction::MoveEmitterDown},
             std::pair{row.remove, kb::editor::ParticleEditorPanelAction::RemoveEmitter},
         }) {
        const POINT point = Center(fixture.first);
        const kb::editor::ParticleEditorPanelHit hit =
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*unscrolled, point.x, point.y);
        Require(hit.action == fixture.second && hit.emitterId == rows[3].emitterId &&
                hit.authoringOrder == rows[3].authoringOrder,
            "emitter row hit target lost selected stable-id focus");
    }
    Require(kb::editor::ParticleEditorPanelLayoutResolver::ReorderTargetAt(
                *unscrolled, unscrolled->emitterRows[5].bounds.top) == 5U,
        "drag target hit test did not resolve authored order");
}

void TestFullWidthNormalizedSlidersAndColorSwatch() {
    const auto rows = Rows(1U);
    kb::particle_editor::ParticleEmitterInspectorView inspector;
    inspector.emitterId = rows[0].emitterId;
    inspector.properties.push_back({.property = kb::particle_editor::ParticleEditorProperty::SpawnRandomization,
        .label = "Randomization", .value = "1", .editable = true,
        .widget = kb::particle_editor::ParticleEditorPropertyWidget::Slider,
        .numericMin = 0.0F, .numericMax = 1.0F, .numericValue = 1.0F});
    inspector.properties.push_back({.property = kb::particle_editor::ParticleEditorProperty::SpawnStartColor,
        .label = "Start color", .value = "1 0 0 1", .editable = true,
        .widget = kb::particle_editor::ParticleEditorPropertyWidget::Color,
        .colorValue = {1.0F, 0.0F, 0.0F, 1.0F}});
    inspector.properties.push_back({.property = kb::particle_editor::ParticleEditorProperty::ModulePayload,
        .moduleId = 7U, .label = "Gradient", .value = "0,1,1,1,1;1,0,0,0,1", .editable = true,
        .widget = kb::particle_editor::ParticleEditorPropertyWidget::Gradient,
        .gradient = {.stops = {{.time = 0.0F, .color = {1.0F, 1.0F, 1.0F, 1.0F}},
                               {.time = 1.0F, .color = {0.0F, 0.0F, 0.0F, 1.0F}}}}});
    inspector.modules.push_back({.moduleId = 7U, .authoringOrder = 0U,
        .type = kb::scene::ParticleModuleType::ColorOverLife, .label = "Color Over Life",
        .enabled = true, .selected = true});
    const RECT content{0, 0, 1280, 720};
    const auto layout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U, &inspector));
    Require(layout->propertyRowCount == 3U && layout->propertyRows[0].hasSlider,
        "normalized slider fixture did not expose a slider row");
    const LONG trackWidth = layout->propertyRows[0].sliderTrack.right - layout->propertyRows[0].sliderTrack.left;
    const LONG rowWidth = layout->propertyRows[0].bounds.right - layout->propertyRows[0].bounds.left;
    Require(trackWidth >= rowWidth - 20,
        "0-1 slider track did not span the full property row");
    Require(std::abs(kb::editor::ParticleEditorPanelLayoutResolver::SliderValueAt(
                layout->propertyRows[0].sliderTrack, layout->propertyRows[0].sliderTrack.left, 0.0F, 1.0F)) < 0.001F &&
            std::abs(kb::editor::ParticleEditorPanelLayoutResolver::SliderValueAt(
                layout->propertyRows[0].sliderTrack, layout->propertyRows[0].sliderTrack.right, 0.0F, 1.0F) - 1.0F) < 0.001F,
        "0-1 slider range did not map the full track to 0 and 1");
    Require(layout->propertyRows[0].sliderFill.right >= layout->propertyRows[0].sliderTrack.right - 1,
        "a 0-1 slider at 1.0 did not fill the whole track");
    const POINT swatch = Center(layout->propertyRows[1].colorSwatch);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*layout, swatch.x, swatch.y).action ==
            kb::editor::ParticleEditorPanelAction::EditProperty,
        "start color swatch was not hittable");
    Require(layout->propertyRows[1].colorSwatch.right - layout->propertyRows[1].colorSwatch.left >= rowWidth / 2,
        "start color swatch did not use the remaining property row");
    const LONG gradientWidth = layout->propertyRows[2].curvePreview.right - layout->propertyRows[2].curvePreview.left;
    Require(gradientWidth >= rowWidth / 2,
        "0-1 gradient preview stayed on a short segment instead of the remaining row width");
}

void TestSortEnumChipsWrapAndStayReadable() {
    const auto rows = Rows(1U);
    kb::particle_editor::ParticleEmitterInspectorView inspector;
    inspector.emitterId = rows[0].emitterId;
    kb::particle_editor::ParticleEditorPropertyRow sort;
    sort.property = kb::particle_editor::ParticleEditorProperty::OutputSort;
    sort.label = "Sort";
    sort.value = "1";
    sort.editable = true;
    sort.widget = kb::particle_editor::ParticleEditorPropertyWidget::Enum;
    sort.enumValue = 1U;
    sort.enumCount = 5U;
    sort.enumLabels = {"None", "Back to Front", "Front to Back", "Distance", "Age", nullptr, nullptr, nullptr};
    inspector.properties.push_back(sort);
    const RECT content{0, 0, 1280, 720};
    const auto layout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U, &inspector));
    Require(layout->propertyRowCount == 1U && layout->propertyRows[0].enumChipCount == 5U,
        "sort enum fixture did not expose five chips");
    const auto& chips = layout->propertyRows[0].enumChips;
    LONG minWidth = chips[0].right - chips[0].left;
    LONG maxBottom = chips[0].bottom;
    LONG minTop = chips[0].top;
    for (std::uint8_t index = 0U; index < 5U; ++index) {
        const LONG width = chips[index].right - chips[index].left;
        minWidth = std::min(minWidth, width);
        maxBottom = std::max(maxBottom, chips[index].bottom);
        minTop = std::min(minTop, chips[index].top);
        Require(width >= 88,
            "sort chip was too narrow to render Back to Front / Front to Back");
        const POINT point = Center(chips[index]);
        const kb::editor::ParticleEditorPanelHit hit =
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*layout, point.x, point.y);
        Require(hit.action == kb::editor::ParticleEditorPanelAction::EditProperty &&
                hit.propertyIndex == 0U && hit.propertyChoice == index,
            "sort chip hit test lost the selected choice");
    }
    Require(maxBottom - minTop >= 44,
        "sort chips stayed on one cramped row instead of wrapping");
}

void TestUnifiedInspectorStreamAndModuleHitTargets() {
    const auto rows = Rows(2U);
    kb::particle_editor::ParticleEmitterInspectorView inspector;
    inspector.emitterId = rows[0].emitterId;
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        inspector.outputChoices.push_back({.type = static_cast<kb::scene::ParticleOutputType>(index),
            .label = "Output", .enabled = index < 3U});
    }
    inspector.properties.push_back({.property = kb::particle_editor::ParticleEditorProperty::SpawnLifetimeMin,
        .label = "Lifetime min", .value = "1", .editable = true,
        .widget = kb::particle_editor::ParticleEditorPropertyWidget::Slider,
        .numericMin = 0.05F, .numericMax = 10.0F, .numericValue = 1.0F});
    inspector.modules.push_back({.moduleId = 41U, .authoringOrder = 0U,
        .type = kb::scene::ParticleModuleType::Gravity, .label = "Gravity", .enabled = true});
    inspector.modules.push_back({.moduleId = 99U, .authoringOrder = 1U,
        .type = kb::scene::ParticleModuleType::Drag, .label = "Drag", .enabled = true});
    inspector.dependencies.push_back({.assetId = kb::assets::AssetId{7U}, .virtualPath = "/Game/Material.21kb"});
    inspector.diagnostics.push_back({.code = kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
        .propertyPath = "effect.emitter[0].output.type", .emitterId = rows[0].emitterId,
        .message = "unavailable"});
    const RECT content{0, 0, 1280, 720};
    const auto layout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U, &inspector));
    Require(layout->outputChoiceCount == 8U && layout->propertyRowCount == 1U &&
            layout->moduleRowCount == 2U && layout->dependencyRowCount == 1U &&
            layout->diagnosticRowCount == 1U,
        "particle inspector did not form one complete bounded composer stream");
    Require(layout->outputChoices[0].top == layout->outputChoices[1].top &&
            layout->outputChoices[0].bottom == layout->outputChoices[1].bottom &&
            layout->outputChoices[2].top > layout->outputChoices[0].top &&
            layout->outputChoices[0].right < layout->outputChoices[1].left &&
            layout->propertyHeader.top < layout->moduleHeader.top &&
            layout->moduleHeader.top < layout->outputHeader.top,
        "composer did not preserve the settings, behavior, and output section order");
    const POINT unsupported = Center(layout->outputChoices[3]);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*layout, unsupported.x, unsupported.y).outputType ==
            kb::scene::ParticleOutputType::Mesh,
        "visible disabled output choice lost its typed identity");
    const POINT property = Center(layout->propertyRows[0].valueBox);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*layout, property.x, property.y).action ==
            kb::editor::ParticleEditorPanelAction::EditProperty,
        "typed property row was not routed");
    const POINT slider = Center(layout->propertyRows[0].sliderTrack);
    Require(layout->propertyRows[0].hasSlider &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*layout, slider.x, slider.y).action ==
                kb::editor::ParticleEditorPanelAction::DragPropertySlider &&
            std::abs(kb::editor::ParticleEditorPanelLayoutResolver::SliderValueAt(
                layout->propertyRows[0].sliderTrack, layout->propertyRows[0].sliderTrack.left, 0.05F, 10.0F) - 0.05F) < 0.001F,
        "numeric property slider lost its drag route or range mapping");
    const auto recipeLayout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U, &inspector, 2U));
    const POINT recipe = Center(recipeLayout->recipeTiles[1]);
    Require(recipeLayout->recipeTileCount == 2U &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*recipeLayout, recipe.x, recipe.y).action ==
                kb::editor::ParticleEditorPanelAction::AppendRecipe &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*recipeLayout, recipe.x, recipe.y).recipeIndex == 1U,
        "recipe tile did not preserve its registry ordering during hit testing");
    kb::particle_editor::ParticleEditorWorkspaceState workspace;
    workspace.ToggleComposerSection(kb::particle_editor::ParticleEditorComposerSection::Output);
    const auto collapsedOutput = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U, &inspector, 0U, &workspace));
    const POINT outputHeader = Center(collapsedOutput->outputHeader);
    Require(collapsedOutput->outputChoiceCount == 0U &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(
                *collapsedOutput, outputHeader.x, outputHeader.y).action ==
                kb::editor::ParticleEditorPanelAction::ToggleComposerSection &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(
                *collapsedOutput, outputHeader.x, outputHeader.y).composerSection ==
                kb::particle_editor::ParticleEditorComposerSection::Output,
        "collapsed composer output section lost its state or header interaction");
    Require(kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(*layout, 96U) >= 0,
        "composer scroll contract must remain defined at a standard authoring height");
    const RECT compactContent{0, 0, 900, 300};
    const auto compactLayout = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(compactContent, rows, 0, 96U, &inspector, 2U));
    const int maximumScroll = kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(*compactLayout, 96U);
    const int moduleScroll = std::clamp(
        static_cast<int>(compactLayout->moduleRows[1].bounds.bottom - compactLayout->emitterList.bottom),
        0,
        maximumScroll);
    const auto moduleScrolled = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
            compactContent, rows, moduleScroll, 96U, &inspector, 2U));
    const POINT grip = Center(moduleScrolled->moduleRows[1].dragGrip);
    const auto moduleHit = kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*moduleScrolled, grip.x, grip.y);
    Require(moduleHit.action == kb::editor::ParticleEditorPanelAction::BeginModuleDrag &&
            moduleHit.moduleId == 99U && moduleHit.authoringOrder == 1U &&
            kb::editor::ParticleEditorPanelLayoutResolver::ModuleReorderTargetAt(
                *moduleScrolled, moduleScrolled->moduleRows[0].bounds.top) == 0U,
        "module drag hit lost stable identity or authored order");
    const auto scrolled = std::make_unique<kb::editor::ParticleEditorPanelLayout>(
        kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
            compactContent, rows, maximumScroll, 96U, &inspector, 2U));
    const POINT dependency = Center(scrolled->dependencyRows[0]);
    const POINT diagnostic = Center(scrolled->diagnosticRows[0]);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*scrolled, dependency.x, dependency.y).action ==
                kb::editor::ParticleEditorPanelAction::NavigateDependency &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(*scrolled, diagnostic.x, diagnostic.y).action ==
                kb::editor::ParticleEditorPanelAction::NavigateDiagnostic &&
            maximumScroll > 0,
        "dependency/diagnostic navigation or unified stream scrolling was not exposed");
}

} // namespace

int main() {
    try {
        TestTwoToOneResponsiveGeometry();
        TestCompactAuthoringMetrics();
        TestHitTestingScrollAndSelectionFocus();
        TestFullWidthNormalizedSlidersAndColorSwatch();
        TestSortEnumChipsWrapAndStayReadable();
        TestUnifiedInspectorStreamAndModuleHitTargets();
        std::cout << "21kb Particle System editor authoring tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
#else
int main() { return 0; }
#endif
