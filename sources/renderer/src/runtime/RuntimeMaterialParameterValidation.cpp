#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "runtime/RuntimeMaterialGraphDependencyLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

struct CachedMaterialParameterSchema {
    bool initialized = false;
    std::uint64_t materialContentHash = 0U;
    std::uint64_t sourceGraphAssetId = 0U;
    std::string sourceGraphPath;
    std::uint64_t sourceGraphContentHash = 0U;
    std::vector<RenderMaterialParameterSchema> parameters;
};

[[nodiscard]] const kb::assets::AssetMetadata* ResolveSourceGraphMetadata(
    const kb::assets::AssetManager& assets,
    const kb::assets::AssetMetadata& owner,
    std::uint64_t assetId,
    std::string_view path) noexcept {
    RenderMaterialAssetData reference;
    reference.graphSourceAssetId = assetId;
    reference.graphSourceAssetPath = path;
    return ResolveRenderMaterialSourceGraphMetadata(assets.Registry(), owner, reference);
}

[[nodiscard]] bool CacheIsCurrent(
    const kb::assets::AssetManager& assets,
    const kb::assets::AssetMetadata& material,
    const CachedMaterialParameterSchema& cached) noexcept {
    if (!cached.initialized || cached.materialContentHash != material.contentHash) {
        return false;
    }
    const kb::assets::AssetMetadata* source =
        ResolveSourceGraphMetadata(
            assets, material, cached.sourceGraphAssetId, cached.sourceGraphPath);
    const std::uint64_t currentSourceHash = source == nullptr ? 0U : source->contentHash;
    return currentSourceHash == cached.sourceGraphContentHash;
}

[[nodiscard]] CachedMaterialParameterSchema LoadSchema(
    kb::assets::AssetManager& assets,
    const kb::assets::AssetMetadata& material) {
    CachedMaterialParameterSchema cached{};
    cached.initialized = true;
    cached.materialContentHash = material.contentHash;

    const kb::assets::AssetHandle<RenderMaterialAssetData> loaded =
        assets.Load<RenderMaterialAssetData>(material.id);
    if (!loaded.IsLoaded()) {
        return cached;
    }

    cached.sourceGraphAssetId = loaded->graphSourceAssetId;
    cached.sourceGraphPath = loaded->graphSourceAssetPath;
    const kb::assets::AssetMetadata* source = ResolveSourceGraphMetadata(
        assets, material, cached.sourceGraphAssetId, cached.sourceGraphPath);
    cached.sourceGraphContentHash = source == nullptr ? 0U : source->contentHash;

    const RuntimeMaterialSourceGraphLoadResult authoritativeGraph =
        LoadRuntimeMaterialSourceGraph(assets, material, *loaded);
    if (!authoritativeGraph.graph.has_value()) {
        return cached;
    }

    RenderMaterialTypeSchema schema =
        BuildRenderMaterialGraphParameterSchema(
            *authoritativeGraph.graph, "runtime.parameter.validation", 1U);
    cached.parameters = std::move(schema.parameters);
    return cached;
}

class RuntimeMaterialParameterSchemaValidator final
    : public kb::scene::MaterialParameterSchemaValidator {
public:
    [[nodiscard]] bool Validate(
        kb::assets::AssetManager& assets,
        std::uint64_t parentMaterialAssetId,
        std::string_view name,
        kb::scene::MaterialParameterType type) const noexcept override {
        if (parentMaterialAssetId == 0U || name.empty()) {
            return false;
        }
        try {
            const kb::assets::AssetMetadata* material =
                assets.Registry().Find(kb::assets::AssetId{ parentMaterialAssetId });
            if (material == nullptr || material->type != "RenderMaterial") {
                return false;
            }

            std::scoped_lock lock{ mutex_ };
            CachedMaterialParameterSchema& cached = schemas_[parentMaterialAssetId];
            if (!CacheIsCurrent(assets, *material, cached)) {
                cached = LoadSchema(assets, *material);
            }
            for (const RenderMaterialParameterSchema& parameter : cached.parameters) {
                if (parameter.name != name ||
                    !parameter.overrideSupported ||
                    parameter.runtimeSupport != RenderMaterialFeatureSupport::Supported) {
                    continue;
                }
                const RenderMaterialParameterType expected =
                    type == kb::scene::MaterialParameterType::Scalar
                    ? RenderMaterialParameterType::Scalar
                    : RenderMaterialParameterType::Bool;
                return parameter.type == expected;
            }
        } catch (...) {
            // The public scene mutation is noexcept. A loader/allocation failure is an
            // honest validation failure and never permits an unchecked override.
        }
        return false;
    }

private:
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::uint64_t, CachedMaterialParameterSchema> schemas_;
};

} // namespace

void InstallRuntimeMaterialParameterValidation(kb::scene::Scene& scene) {
    if (scene.MaterialInstances().HasParameterSchemaValidator()) {
        return;
    }
    scene.MaterialInstances().SetParameterSchemaValidator(
        std::make_shared<RuntimeMaterialParameterSchemaValidator>());
}

} // namespace kb::render
