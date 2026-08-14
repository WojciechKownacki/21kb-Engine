#pragma once

#include "editor/ParticleBakeService.hpp"

#include <filesystem>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
} // namespace kb::assets

namespace kb::editor {

class EditorConsoleState;

class ParticleEditorBakeHostCommand final {
  public:
    ParticleEditorBakeHostCommand() = delete;
    [[nodiscard]] static kb::particle_editor::ParticleBakeResult Execute(
        const kb::scene::ParticleEffectAsset& workingAsset,
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry,
        const std::filesystem::path& projectRoot,
        EditorConsoleState& console);
};

} // namespace kb::editor
