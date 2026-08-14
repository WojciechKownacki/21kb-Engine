#include "editor/ParticleAssetGateway.hpp"
#include "editor/ParticleDocumentCloseGuard.hpp"
#include "editor/ParticleEditorDocument.hpp"
#include "editor/ParticlePreviewSession.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"

#include <bgfx/bgfx.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef KB_21KB_PARTICLE_PLUGIN_PATH
#define KB_21KB_PARTICLE_PLUGIN_PATH ""
#endif

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeEffect(float rate = 60.0F) {
    kb::scene::ParticleEffectAsset effect;
    effect.effectId = 7001U;
    effect.displayName = "Editor Preview Effect";
    effect.recipeCategory = "Simple";
    effect.determinismSeed = 0xA1B2C3D4E5F60718ULL;
    effect.durationSeconds = 5.0F;
    effect.looping = true;
    kb::scene::ParticleEmitterAsset emitter;
    emitter.emitterId = 11U;
    emitter.name = "Preview Emitter";
    emitter.maxParticles = 512U;
    emitter.spawn.rateOverTime.keyframes = {{.time = 0.0F, .value = rate}};
    emitter.spawn.lifetimeMin = 2.0F;
    emitter.spawn.lifetimeMax = 2.0F;
    emitter.spawn.direction = {1.0F, 0.0F, 0.0F};
    emitter.spawn.speedMin = 1.0F;
    emitter.spawn.speedMax = 1.0F;
    emitter.output.material.virtualPath = "/Game/Materials/PreviewParticle.21kb";
    effect.emitters.push_back(std::move(emitter));
    return effect;
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_particle_editor_tests";
}

[[nodiscard]] std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void TestDocumentHistorySavePointAndAtomicFailure() {
    const std::filesystem::path root = TestRoot() / "document";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "document fixture directory creation failed");

    kb::particle_editor::ParticleAssetGateway gateway;
    kb::particle_editor::ParticleEditorDocument document;
    Require(document.Create(MakeEffect()).Succeeded(), "new particle document creation failed");
    Require(document.Dirty() && !document.SessionPath().has_value(),
        "new unsaved particle document did not start dirty and pathless");
    Require(std::filesystem::is_empty(root), "creating an unsaved document touched disk");

    auto changed = document.Asset();
    changed.displayName = "Changed Preview Effect";
    Require(document.Apply(changed).Succeeded() && document.CanUndo(),
        "particle document edit was not recorded");
    Require(document.Undo() && document.Asset().displayName == "Editor Preview Effect",
        "particle document undo did not restore the prior asset");
    Require(document.Redo() && document.Asset().displayName == "Changed Preview Effect",
        "particle document redo did not restore the edit");
    Require(document.Apply(document.Asset()).status == kb::particle_editor::ParticleEditorStatus::NoChange,
        "canonical no-op edit polluted particle history");

    const std::filesystem::path failedPath = root / "CannotSave.kbvfx";
    const std::string oldBytes = "existing bytes remain";
    {
        std::ofstream output{failedPath, std::ios::binary};
        output << oldBytes;
    }
    std::filesystem::create_directory(failedPath.string() + ".tmp", error);
    Require(!error, "atomic failure fixture could not reserve the temporary path");
    const auto failed = document.Save(gateway, failedPath);
    Require(!failed.Succeeded() && failed.status == kb::particle_editor::ParticleEditorStatus::IoFailure,
        "particle document atomic save failure was not explicit");
    Require(!document.SessionPath().has_value() && document.Dirty() && ReadBytes(failedPath) == oldBytes,
        "failed first save adopted a path, cleared dirty state, or modified destination bytes");

    const std::filesystem::path savedPath = root / "Saved.kbvfx";
    Require(document.Save(gateway, savedPath).Succeeded(), "particle document first save failed");
    Require(document.SessionPath() == savedPath && !document.Dirty(),
        "successful first save did not atomically establish path and save point");
    changed = document.Asset();
    changed.durationSeconds = 7.0F;
    Require(document.Apply(changed).Succeeded() && document.Dirty(), "post-save edit was not dirty");
    Require(document.Revert() && !document.Dirty() && document.Asset().durationSeconds == 5.0F,
        "revert did not restore the exact saved particle asset");

    kb::particle_editor::ParticleEditorDocument opened;
    Require(opened.Open(gateway, savedPath).Succeeded() && !opened.Dirty(),
        "saved particle document did not reopen cleanly");
    const std::filesystem::path malformed = root / "Malformed.kbvfx";
    {
        std::ofstream output{malformed, std::ios::binary};
        output << "not a particle effect\n";
    }
    Require(!opened.Open(gateway, malformed).Succeeded() && opened.SessionPath() == savedPath &&
            opened.Asset().displayName == "Changed Preview Effect",
        "failed open damaged the active document session");
}

void TestCloseGuardAllDirtyTransitions() {
    kb::particle_editor::ParticleAssetGateway gateway;
    kb::particle_editor::ParticleEditorDocument document;
    Require(document.Create(MakeEffect()).Succeeded(), "close-guard document creation failed");
    kb::particle_editor::ParticleDocumentCloseGuard guard;
    for (const auto transition : {
             kb::particle_editor::ParticleDocumentTransition::Open,
             kb::particle_editor::ParticleDocumentTransition::Revert,
             kb::particle_editor::ParticleDocumentTransition::CloseTab,
             kb::particle_editor::ParticleDocumentTransition::CloseWindow,
             kb::particle_editor::ParticleDocumentTransition::CloseProject,
             kb::particle_editor::ParticleDocumentTransition::ExitApplication}) {
        Require(guard.Request(document, transition).state ==
                kb::particle_editor::ParticleDocumentCloseState::DecisionRequired,
            "dirty transition bypassed the document close guard");
        Require(guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Cancel,
                    document, gateway).state == kb::particle_editor::ParticleDocumentCloseState::Cancelled,
            "cancel did not block a dirty particle transition");
    }
    Require(guard.Request(document, kb::particle_editor::ParticleDocumentTransition::CloseTab).state ==
            kb::particle_editor::ParticleDocumentCloseState::DecisionRequired,
        "dirty close did not request a decision");
    const auto blocked = guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Save,
        document, gateway);
    Require(blocked.state == kb::particle_editor::ParticleDocumentCloseState::Blocked &&
            blocked.saveResult.status == kb::particle_editor::ParticleEditorStatus::PathRequired &&
            guard.PendingTransition().has_value(),
        "failed close-time save did not preserve the pending transition");
    Require(guard.Resolve(kb::particle_editor::ParticleDocumentCloseDecision::Discard,
                document, gateway).state == kb::particle_editor::ParticleDocumentCloseState::Proceed,
        "explicit discard did not release the pending transition");
}

class HeadlessSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 32U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 32U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

void TestIsolatedRuntimePreviewAndGpuRelease() {
    const std::filesystem::path pluginPath = KB_21KB_PARTICLE_PLUGIN_PATH;
    Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath),
        "focused editor preview test requires the produced provider module");
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({
        .name = "Rendering.21kbParticle",
        .binaryPath = pluginPath.string(),
        .enabled = true,
    });

    kb::assets::AssetRegistry sourceRegistry;
    Require(sourceRegistry.Upsert({
                .id = kb::assets::AssetId{72U},
                .type = "RenderMaterial",
                .name = "Preview Particle Material",
                .virtualPath = "/Game/Materials/PreviewParticle.21kb",
                .physicalPath = "PreviewParticle.21kb",
                .contentHash = 1U,
            }),
        "preview dependency metadata registration failed");

    kb::particle_editor::ParticlePreviewSession preview;
    const auto effect = MakeEffect();
    Require(preview.Start(project, sourceRegistry, kb::assets::AssetId{71U},
                "/Game/Effects/UnsavedPreview.kbvfx", effect).Succeeded(),
        "isolated particle preview session did not start through the real provider");
    Require(preview.Active() && preview.PreviewScene().Mode() == kb::scene::SceneMode::Runtime &&
            preview.PreviewScene().IsModuleActive("Rendering.21kbParticle") &&
            kb::particles::ParticlePlayback::HasBackend(preview.PreviewScene()) &&
            preview.PreviewScene().Components().ParticleEffects().Has(preview.EffectEntity()) &&
            preview.PreviewScene().Components().Cameras().Has(preview.CameraEntity()),
        "preview did not own the required runtime scene, provider, component, and camera");

    for (int frame = 0; frame < 4; ++frame) {
        Require(preview.Tick(1.0F / 60.0F).Succeeded(), "preview SceneRuntime update failed");
    }
    const auto instances = kb::particles::ParticlePlayback::LiveInstanceIds(preview.PreviewScene());
    Require(instances.size() == 1U &&
            kb::particles::ParticlePlayback::Query(preview.PreviewScene(), instances.front()).liveParticleCount > 0U,
        "preview did not use the accepted runtime simulation backend");
    auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(preview.PreviewScene());
    Require(snapshot != nullptr && !snapshot->IsTombstone() && !snapshot->Particles().empty(),
        "preview runtime did not publish a GPU-consumable particle snapshot");
    std::weak_ptr<const kb::particles::ParticleRenderSnapshot> snapshotLifetime = snapshot;
    snapshot.reset();

    auto workingCopy = effect;
    workingCopy.displayName = "Unsaved Runtime Working Copy";
    workingCopy.emitters.front().spawn.rateOverTime.keyframes.front().value = 120.0F;
    Require(preview.PublishWorkingCopy(workingCopy).Succeeded(),
        "unsaved working copy was not published through AssetManager");
    Require(preview.Tick(1.0F / 60.0F).Succeeded(),
        "preview did not reconcile the unsaved runtime publication");
    Require(!std::filesystem::exists(TestRoot() / "UnsavedPreview.kbvfx"),
        "publishing an unsaved preview working copy touched disk");

    HeadlessSurface surface;
    kb::render::DisplayConfig display{};
    display.allowHeadlessNoop = true;
    display.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    kb::render::Renderer renderer;
    Require(renderer.Initialize(surface, &display),
        "accepted GPU renderer did not initialize for particle preview");
    Require(renderer.BeginFrame() && preview.Submit(renderer),
        "particle preview did not submit through the accepted GPU renderer");
    renderer.EndFrame();
    Require(renderer.RuntimeResourceStats().renderSceneCount == 1U,
        "GPU renderer did not retain exactly one isolated preview scene");
    preview.Release(renderer);
    Require(!preview.Active() && renderer.RuntimeResourceStats().renderSceneCount == 0U &&
            snapshotLifetime.expired(),
        "preview release leaked its runtime scene, retained snapshot, or renderer-owned scene cache");
    renderer.Shutdown();
}

} // namespace

int main() {
    try {
        TestDocumentHistorySavePointAndAtomicFailure();
        TestCloseGuardAllDirtyTransitions();
        TestIsolatedRuntimePreviewAndGpuRelease();
        std::cout << "21kb Particle System editor core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
