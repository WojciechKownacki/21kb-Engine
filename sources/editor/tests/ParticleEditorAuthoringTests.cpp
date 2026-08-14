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

} // namespace

int main() {
    try {
        TestTwoToOneResponsiveGeometry();
        TestHitTestingScrollAndSelectionFocus();
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
