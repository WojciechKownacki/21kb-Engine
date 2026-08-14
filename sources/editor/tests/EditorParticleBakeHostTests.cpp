#include "console/EditorConsoleState.hpp"
#include "scene/ParticleEditorBakeHostCommand.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeEffect() {
    kb::scene::ParticleEffectAsset effect;
    effect.effectId = 1U;
    effect.displayName = "Host Bake Effect";
    effect.recipeCategory = "Simple";
    effect.durationSeconds = 2.0F;
    kb::scene::ParticleEmitterAsset emitter;
    emitter.emitterId = 1U;
    emitter.name = "Host Emitter";
    emitter.output.material = {.assetId = 2U};
    effect.emitters.push_back(std::move(emitter));
    return effect;
}

void RunHostBakeCommandTest() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_particle_editor_host_bake_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "host Bake test root creation failed");
    const std::filesystem::path sourcePath = root / "Assets" / "Host.kbvfx";
    std::filesystem::create_directories(sourcePath.parent_path(), error);
    Require(!error, "host Bake source directory creation failed");
    {
        std::ofstream source{sourcePath, std::ios::binary};
        source << "source bytes owned by the editor document gateway";
    }
    const std::string sourceBytes = ReadBytes(sourcePath);

    kb::assets::AssetRegistry registry;
    const kb::assets::AssetMetadata owner{.id = kb::assets::AssetId{1U},
        .type = kb::scene::kParticleEffectAssetType, .virtualPath = "/Game/Host.kbvfx",
        .physicalPath = sourcePath, .contentHash = 1U};
    Require(registry.Upsert(owner) && registry.Upsert({.id = kb::assets::AssetId{2U},
                .type = "RenderMaterial", .virtualPath = "/Game/Material.kbmat", .contentHash = 2U}) &&
            registry.Upsert({.id = kb::assets::AssetId{3U},
                .type = "RenderMesh", .virtualPath = "/Game/Mesh.kbmesh", .contentHash = 3U}),
        "host Bake registry setup failed");

    kb::editor::EditorConsoleState console;
    const kb::scene::ParticleEffectAsset working = MakeEffect();
    const auto baked = kb::editor::ParticleEditorBakeHostCommand::Execute(
        working, owner, registry, root, console);
    const std::filesystem::path expectedRoot = root / "Saved" / "21kbParticleCache";
    Require(baked.Succeeded() && baked.cachePath.parent_path().parent_path() == expectedRoot &&
            std::filesystem::is_regular_file(baked.cachePath) && ReadBytes(sourcePath) == sourceBytes &&
            console.Count(kb::editor::EditorConsoleLevel::Info) == 1U &&
            console.Count(kb::editor::EditorConsoleLevel::Error) == 0U,
        "production host Bake did not use the project cache root or mutated the source document");

    auto unsupported = working;
    unsupported.emitters[0].output.type = kb::scene::ParticleOutputType::Mesh;
    unsupported.emitters[0].output.mesh = {.assetId = 3U};
    unsupported.emitters[0].output.payload = kb::scene::ParticleMeshOutput{};
    const auto rejected = kb::editor::ParticleEditorBakeHostCommand::Execute(
        unsupported, owner, registry, root, console);
    const kb::editor::EditorConsoleEntry& diagnostic = console.Entries().back();
    Require(rejected.status == kb::particle_editor::ParticleBakeStatus::UnsupportedCapability &&
            !rejected.diagnostics.empty() &&
            rejected.diagnostics.front().code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability &&
            diagnostic.level == kb::editor::EditorConsoleLevel::Error &&
            diagnostic.message.find("effect.emitter[0].output.type") != std::string::npos &&
            ReadBytes(sourcePath) == sourceBytes,
        "host Bake did not surface the typed unsupported diagnostic or changed source bytes");
}

} // namespace

int main() {
    try {
        RunHostBakeCommandTest();
        std::cout << "21kb Particle System editor host Bake tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
