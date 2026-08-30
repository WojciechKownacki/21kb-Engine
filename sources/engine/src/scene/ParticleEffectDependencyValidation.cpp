#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include "engine/assets/AssetKind.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::uint32_t FindPropertyLine(const std::filesystem::path& path, std::string_view propertyPath) {
    std::ifstream input{path, std::ios::binary};
    std::string line;
    std::uint32_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (std::string_view{line}.starts_with(propertyPath) && line.size() > propertyPath.size() &&
            (line[propertyPath.size()] == '.' || line[propertyPath.size()] == ' '))
            return lineNumber;
    }
    return 0U;
}

void AddDiagnostic(ParticleEffectDependencyResult& result, ParticleEffectDiagnosticCode code,
                   const kb::assets::AssetMetadata& owner, std::string path, std::string message,
                   ParticleStableId emitterId = 0U, ParticleStableId moduleId = 0U) {
    if (result.diagnostics.size() > kParticleEffectMaxDiagnostics)
        return;
    if (result.diagnostics.size() == kParticleEffectMaxDiagnostics) {
        result.diagnostics.push_back(
            ParticleEffectDiagnostic{.code = ParticleEffectDiagnosticCode::LimitExceeded,
                                     .line = FindPropertyLine(owner.physicalPath, path),
                                     .propertyPath = std::move(path),
                                     .emitterId = emitterId,
                                     .moduleId = moduleId,
                                     .message = "dependency diagnostic collection reached its schema hard limit"});
        return;
    }
    result.diagnostics.push_back(ParticleEffectDiagnostic{.code = code,
                                                          .line = FindPropertyLine(owner.physicalPath, path),
                                                          .propertyPath = std::move(path),
                                                          .emitterId = emitterId,
                                                          .moduleId = moduleId,
                                                          .message = std::move(message)});
}

struct DependencyReference {
    const ParticleAssetReference& reference;
    std::string path;
    std::optional<kb::assets::AssetKind> expectedKind;
    bool expectsParticleEffect = false;
    ParticleStableId emitterId = 0U;
    ParticleStableId moduleId = 0U;
};

[[nodiscard]] const kb::assets::AssetMetadata* Resolve(const DependencyReference& dependency,
                                                       const kb::assets::AssetMetadata& owner,
                                                       const kb::assets::AssetRegistry& registry,
                                                       ParticleEffectDependencyResult& result) {
    const bool hasId = dependency.reference.assetId != 0U;
    const bool hasPath = !dependency.reference.virtualPath.empty();
    if (!hasId && !hasPath) {
        AddDiagnostic(result, ParticleEffectDiagnosticCode::MissingDependency, owner, dependency.path,
                      "dependency reference has neither an asset id nor a virtual path", dependency.emitterId,
                      dependency.moduleId);
        return nullptr;
    }
    const kb::assets::AssetMetadata* byId =
        hasId ? registry.Find(kb::assets::AssetId{dependency.reference.assetId}) : nullptr;
    const kb::assets::AssetMetadata* byPath =
        hasPath ? registry.FindByPath(std::filesystem::path{dependency.reference.virtualPath}) : nullptr;
    if (hasId && byId == nullptr) {
        AddDiagnostic(result, ParticleEffectDiagnosticCode::MissingDependency, owner, dependency.path + ".assetId",
                      "dependency asset id does not resolve", dependency.emitterId, dependency.moduleId);
        return nullptr;
    }
    if (hasPath && byPath == nullptr) {
        AddDiagnostic(result, ParticleEffectDiagnosticCode::MissingDependency, owner, dependency.path + ".path",
                      "dependency virtual path does not resolve", dependency.emitterId, dependency.moduleId);
        return nullptr;
    }
    if (byId != nullptr && byPath != nullptr && byId->id != byPath->id) {
        AddDiagnostic(result, ParticleEffectDiagnosticCode::MismatchedReference, owner, dependency.path,
                      "asset id and virtual path resolve to different assets", dependency.emitterId,
                      dependency.moduleId);
        return nullptr;
    }
    const kb::assets::AssetMetadata* resolved = byId != nullptr ? byId : byPath;
    const bool correctType = dependency.expectsParticleEffect
                                 ? resolved->type == kParticleEffectAssetType
                                 : kb::assets::AssetMatchesKind(*resolved, *dependency.expectedKind);
    if (!correctType) {
        AddDiagnostic(result, ParticleEffectDiagnosticCode::WrongAssetType, owner, dependency.path,
                      "dependency resolves to an incompatible asset type", dependency.emitterId, dependency.moduleId);
        return nullptr;
    }
    return resolved;
}

[[nodiscard]] std::vector<DependencyReference> References(const ParticleEffectAsset& asset) {
    std::vector<DependencyReference> references;
    references.reserve(asset.emitters.size() * 3U + asset.eventBindings.size());
    for (std::size_t index = 0U; index < asset.emitters.size(); ++index) {
        const ParticleEmitterAsset& emitter = asset.emitters[index];
        const std::string base = "effect.emitter[" + std::to_string(index) + "].output.";
        references.push_back(
            {emitter.output.material, base + "material", kb::assets::AssetKind::Material, false, emitter.emitterId});
        if (!emitter.output.mesh.Empty())
            references.push_back(
                {emitter.output.mesh, base + "mesh", kb::assets::AssetKind::Mesh, false, emitter.emitterId});
        if (!emitter.output.textureAtlas.Empty())
            references.push_back({emitter.output.textureAtlas, base + "textureAtlas", kb::assets::AssetKind::Texture,
                                  false, emitter.emitterId});
    }
    for (std::size_t index = 0U; index < asset.eventBindings.size(); ++index) {
        const ParticleEventBindingAsset& binding = asset.eventBindings[index];
        if (binding.action == ParticleEventAction::EmitEffectAsset)
            references.push_back({binding.targetEffect,
                                  "effect.eventBinding[" + std::to_string(index) + "].targetEffect", std::nullopt, true,
                                  binding.sourceEmitterId, binding.sourceModuleId});
    }
    return references;
}

} // namespace

static ParticleEffectDependencyResult
ValidateDependenciesImpl(const ParticleEffectAsset& rootAsset, const kb::assets::AssetMetadata& metadata,
                         const kb::assets::AssetRegistry& registry,
                         const std::function<ParticleEffectLoadResult(const kb::assets::AssetMetadata&)>& loadEffect) {
    ParticleEffectDependencyResult analysis;
    std::set<std::uint64_t> directDependencyIds;
    std::set<std::uint64_t> transitiveDependencyIds;
    std::set<std::uint64_t> visited;
    std::vector<std::uint64_t> active;
    std::function<void(const kb::assets::AssetMetadata&, const ParticleEffectAsset&)> visit;
    visit = [&](const kb::assets::AssetMetadata& owner, const ParticleEffectAsset& asset) {
        if (visited.size() >= kParticleEffectMaxDependencyAssets) {
            AddDiagnostic(analysis, ParticleEffectDiagnosticCode::LimitExceeded, owner, "effect",
                          "external dependency graph exceeds the hard asset limit");
            return;
        }
        visited.insert(owner.id.value);
        active.push_back(owner.id.value);
        for (const DependencyReference& dependency : References(asset)) {
            const kb::assets::AssetMetadata* resolved = Resolve(dependency, owner, registry, analysis);
            if (resolved == nullptr)
                continue;
            if (owner.id == metadata.id)
                directDependencyIds.insert(resolved->id.value);
            transitiveDependencyIds.insert(resolved->id.value);
            if (!dependency.expectsParticleEffect)
                continue;
            if (std::find(active.begin(), active.end(), resolved->id.value) != active.end()) {
                AddDiagnostic(analysis, ParticleEffectDiagnosticCode::CyclicReference, owner, dependency.path,
                              "external particle effect dependency contains a cycle", dependency.emitterId,
                              dependency.moduleId);
                continue;
            }
            if (visited.contains(resolved->id.value))
                continue;
            ParticleEffectLoadResult loaded = loadEffect(*resolved);
            if (!loaded.Succeeded()) {
                AddDiagnostic(analysis, ParticleEffectDiagnosticCode::MissingDependency, owner, dependency.path,
                              "external particle effect dependency cannot be loaded", dependency.emitterId,
                              dependency.moduleId);
                continue;
            }
            visit(*resolved, *loaded.asset);
        }
        active.pop_back();
    };

    visit(metadata, rootAsset);
    directDependencyIds.erase(metadata.id.value);
    transitiveDependencyIds.erase(metadata.id.value);
    analysis.dependencies.reserve(directDependencyIds.size());
    for (std::uint64_t id : directDependencyIds)
        analysis.dependencies.push_back(kb::assets::AssetId{id});
    analysis.transitiveDependencies.reserve(transitiveDependencyIds.size());
    for (std::uint64_t id : transitiveDependencyIds)
        analysis.transitiveDependencies.push_back(kb::assets::AssetId{id});
    return analysis;
}

ParticleEffectDependencyResult
ParticleEffectAssetValidator::ValidateDependencies(const kb::assets::AssetMetadata& metadata,
                                                   const kb::assets::AssetRegistry& registry) {
    ParticleEffectLoadResult loaded = ParticleEffectAssetIO::LoadDetailed(metadata.physicalPath);
    if (!loaded.Succeeded()) {
        ParticleEffectDependencyResult result;
        result.diagnostics = std::move(loaded.diagnostics);
        return result;
    }
    return ValidateDependenciesImpl(
        *loaded.asset,
        metadata,
        registry,
        [](const kb::assets::AssetMetadata& dependency) {
            return ParticleEffectAssetIO::LoadDetailed(dependency.physicalPath);
        });
}

ParticleEffectDependencyResult
ParticleEffectAssetValidator::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request,
    const kb::assets::AssetRegistry& registry) {
    std::vector<std::uint8_t> sourceBytes;
    std::string readError;
    if (!request.ReadSourceBytes(sourceBytes, readError)) {
        ParticleEffectDependencyResult result;
        result.diagnostics.push_back(ParticleEffectDiagnostic{
            .code = ParticleEffectDiagnosticCode::FileAccessFailed,
            .message = std::move(readError),
        });
        return result;
    }
    const auto parse = [](std::span<const std::uint8_t> bytes) {
        return ParticleEffectAssetIO::Parse(bytes.empty()
            ? std::string_view{}
            : std::string_view{ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    };
    ParticleEffectLoadResult loaded = parse(sourceBytes);
    if (!loaded.Succeeded()) {
        ParticleEffectDependencyResult result;
        result.diagnostics = std::move(loaded.diagnostics);
        return result;
    }
    return ValidateDependenciesImpl(
        *loaded.asset,
        request.metadata,
        registry,
        [&request, &parse](const kb::assets::AssetMetadata& dependency) {
            std::vector<std::uint8_t> dependencyBytes;
            std::string dependencyError;
            if (!request.ReadDependencySourceBytes(dependency, dependencyBytes, dependencyError)) {
                ParticleEffectLoadResult result;
                result.diagnostics.push_back(ParticleEffectDiagnostic{
                    .code = ParticleEffectDiagnosticCode::FileAccessFailed,
                    .message = std::move(dependencyError),
                });
                return result;
            }
            return parse(dependencyBytes);
        });
}

ParticleEffectDependencyResult
ParticleEffectAssetValidator::ValidateDependencies(const ParticleEffectAsset& workingAsset,
                                                   const kb::assets::AssetMetadata& metadata,
                                                   const kb::assets::AssetRegistry& registry) {
    return ValidateDependenciesImpl(
        workingAsset,
        metadata,
        registry,
        [](const kb::assets::AssetMetadata& dependency) {
            return ParticleEffectAssetIO::LoadDetailed(dependency.physicalPath);
        });
}

} // namespace kb::scene
