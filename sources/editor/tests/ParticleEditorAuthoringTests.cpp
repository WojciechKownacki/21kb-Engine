#include "rendering/ParticleEditorPanelLayout.hpp"

#if defined(_WIN32)
#include <cstdlib>
#include <iostream>
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
        const kb::editor::ParticleEditorPanelLayout layout =
            kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
                fixture.first, rows, 0, fixture.second);
        const LONG previewWidth = layout.preview.right - layout.preview.left;
        const LONG composerWidth = layout.composer.right - layout.composer.left;
        Require(previewWidth >= composerWidth * 2 - 2 && previewWidth <= composerWidth * 2 + 2,
            "particle panel did not preserve the two-to-one preview/composer layout");
        Require(layout.toolbar.top == fixture.first.top && layout.statusBar.bottom == fixture.first.bottom &&
                layout.preview.bottom == layout.composer.bottom,
            "particle panel responsive bands did not fill the content rect");
    }
}

void TestCompactAuthoringMetrics() {
    const auto layout = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
        RECT{0, 0, 1280, 720}, Rows(2U), 0, 96U);
    Require(layout.toolbar.bottom - layout.toolbar.top == 32,
        "particle toolbar did not use the compact production height");
    Require(layout.statusBar.bottom - layout.statusBar.top == 20,
        "particle status bar did not use the compact production height");
    Require(layout.addEmitter.bottom - layout.addEmitter.top == 28,
        "particle primary action did not use a usable compact control height");
    Require(layout.emitterRows[0].bounds.bottom - layout.emitterRows[0].bounds.top == 28,
        "particle emitter row did not preserve compact spacing");
    Require(layout.emitterRows[0].enabledToggle.right - layout.emitterRows[0].enabledToggle.left >= 20 &&
            layout.emitterRows[0].enabledToggle.bottom - layout.emitterRows[0].enabledToggle.top >= 20,
        "emitter action target became too small for reliable pointer use");
}

void TestHitTestingScrollAndSelectionFocus() {
    const auto rows = Rows(8U);
    const RECT content{0, 0, 900, 300};
    const auto unscrolled = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 0, 96U);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(unscrolled, 96U) > 0,
        "bounded emitter list did not expose scrolling in a short composer");
    const auto scrolled = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(content, rows, 72, 96U);
    Require(scrolled.emitterRows[0].bounds.top == unscrolled.emitterRows[0].bounds.top - 72,
        "composer scroll offset did not move emitter rows deterministically");

    const POINT add = Center(unscrolled.addEmitter);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(unscrolled, add.x, add.y).action ==
            kb::editor::ParticleEditorPanelAction::None,
        "full emitter list did not disable the add hit target");
    const auto availableRows = Rows(7U);
    const auto available = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
        content, availableRows, 0, 96U);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(available, add.x, add.y).action ==
            kb::editor::ParticleEditorPanelAction::AddEmitter,
        "available emitter slot did not route the add hit target");
    const auto& row = unscrolled.emitterRows[3];
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
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(unscrolled, point.x, point.y);
        Require(hit.action == fixture.second && hit.emitterId == rows[3].emitterId &&
                hit.authoringOrder == rows[3].authoringOrder,
            "emitter row hit target lost selected stable-id focus");
    }
    Require(kb::editor::ParticleEditorPanelLayoutResolver::ReorderTargetAt(
                unscrolled, unscrolled.emitterRows[5].bounds.top) == 5U,
        "drag target hit test did not resolve authored order");
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
        .label = "Lifetime min", .value = "1", .editable = true});
    inspector.modules.push_back({.moduleId = 41U, .authoringOrder = 0U,
        .type = kb::scene::ParticleModuleType::Gravity, .label = "Gravity", .enabled = true});
    inspector.modules.push_back({.moduleId = 99U, .authoringOrder = 1U,
        .type = kb::scene::ParticleModuleType::Drag, .label = "Drag", .enabled = true});
    inspector.dependencies.push_back({.assetId = kb::assets::AssetId{7U}, .virtualPath = "/Game/Material.21kb"});
    inspector.diagnostics.push_back({.code = kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
        .propertyPath = "effect.emitter[0].output.type", .emitterId = rows[0].emitterId,
        .message = "unavailable"});
    const RECT content{0, 0, 1280, 720};
    const auto layout = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
        content, rows, 0, 96U, &inspector);
    Require(layout.outputChoiceCount == 8U && layout.propertyRowCount == 1U &&
            layout.moduleRowCount == 2U && layout.dependencyRowCount == 1U &&
            layout.diagnosticRowCount == 1U,
        "particle inspector did not form one complete bounded composer stream");
    Require(layout.outputChoices[0].top == layout.outputChoices[1].top &&
            layout.outputChoices[0].bottom == layout.outputChoices[1].bottom &&
            layout.outputChoices[2].top > layout.outputChoices[0].top &&
            layout.outputChoices[0].right < layout.outputChoices[1].left &&
            layout.propertyHeader.top > layout.outputChoices[7].bottom,
        "output choices did not form a compact readable two-column grid before properties");
    const POINT unsupported = Center(layout.outputChoices[3]);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(layout, unsupported.x, unsupported.y).outputType ==
            kb::scene::ParticleOutputType::Mesh,
        "visible disabled output choice lost its typed identity");
    const POINT property = Center(layout.propertyRows[0]);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(layout, property.x, property.y).action ==
            kb::editor::ParticleEditorPanelAction::EditProperty,
        "typed property row was not routed");
    Require(kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(layout, 96U) == 0,
        "compact inspector still required scrolling at a standard authoring height");
    const RECT compactContent{0, 0, 900, 300};
    const auto compactLayout = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
        compactContent, rows, 0, 96U, &inspector);
    const int maximumScroll = kb::editor::ParticleEditorPanelLayoutResolver::MaximumComposerScroll(compactLayout, 96U);
    const auto scrolled = kb::editor::ParticleEditorPanelLayoutResolver::Resolve(
        compactContent, rows, maximumScroll, 96U, &inspector);
    const POINT grip = Center(scrolled.moduleRows[1].dragGrip);
    const auto moduleHit = kb::editor::ParticleEditorPanelLayoutResolver::HitTest(scrolled, grip.x, grip.y);
    Require(moduleHit.action == kb::editor::ParticleEditorPanelAction::BeginModuleDrag &&
            moduleHit.moduleId == 99U && moduleHit.authoringOrder == 1U &&
            kb::editor::ParticleEditorPanelLayoutResolver::ModuleReorderTargetAt(
                scrolled, scrolled.moduleRows[0].bounds.top) == 0U,
        "module drag hit lost stable identity or authored order");
    const POINT dependency = Center(scrolled.dependencyRows[0]);
    const POINT diagnostic = Center(scrolled.diagnosticRows[0]);
    Require(kb::editor::ParticleEditorPanelLayoutResolver::HitTest(scrolled, dependency.x, dependency.y).action ==
                kb::editor::ParticleEditorPanelAction::NavigateDependency &&
            kb::editor::ParticleEditorPanelLayoutResolver::HitTest(scrolled, diagnostic.x, diagnostic.y).action ==
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
