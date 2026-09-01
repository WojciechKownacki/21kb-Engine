#include "PackagedRuntimeModules.hpp"

#include "PackagedRuntimeModuleContract.hpp"

#include "BasicLightingModule.hpp"
#include "JoltPhysicsModule.hpp"
#include "MiniaudioModule.hpp"
#include "ParticleModule.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/script/ScriptModule.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace kb::game {

bool CreatePackagedRuntimeModules(
    const kb::project::ProjectDescriptor& descriptor,
    PackagedRuntimeModules& output,
    std::string& error) {
    error.clear();
    PackagedRuntimeModules candidate{};

    auto script = std::make_unique<kb::script::ScriptModule>();
    candidate.script = script.get();
    candidate.modules.push_back(std::move(script));

    for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        if (!plugin.enabled) {
            continue;
        }
        const std::optional<PackagedRuntimeModuleKind> kind =
            TryPackagedRuntimeModuleKind(plugin.name);
        if (!kind.has_value()) {
            error = "package requires a module that is not linked into this game: " +
                plugin.name;
            return false;
        }
        switch (*kind) {
        case PackagedRuntimeModuleKind::PhysicsJolt:
            candidate.modules.push_back(std::make_unique<kb::physics_jolt::JoltPhysicsModule>());
            break;
        case PackagedRuntimeModuleKind::AudioMiniaudio:
            candidate.modules.push_back(std::make_unique<kb::audio_miniaudio::MiniaudioModule>());
            break;
        case PackagedRuntimeModuleKind::BasicLighting:
            candidate.modules.push_back(std::make_unique<kb::basic_lighting::BasicLightingModule>());
            break;
        case PackagedRuntimeModuleKind::Particle21kb:
            candidate.modules.push_back(std::make_unique<kb::particle_plugin::ParticleModule>());
            break;
        }
    }

    output = std::move(candidate);
    return true;
}

} // namespace kb::game
