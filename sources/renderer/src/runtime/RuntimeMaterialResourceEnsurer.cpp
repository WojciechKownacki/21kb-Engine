#include "runtime/RuntimeMaterialResourceEnsurer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <optional>

namespace kb::render {
namespace {

void EmitUnresolvedMaterialTexturePathDiagnostic(
    SceneRenderDiagnostics& diagnostics,
    std::uint64_t materialAssetId,
    std::uint32_t unresolvedTexturePathCount) {
    if (unresolvedTexturePathCount == 0U) {
        return;
    }

    diagnostics.events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Warning,
        .kind = SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath,
        .materialAssetId = materialAssetId,
        .instanceCount = unresolvedTexturePathCount,
    });
}

[[nodiscard]] SceneRenderDiagnosticSeverity ConvertSeverity(RuntimeMaterialResolveDiagnosticSeverity severity) noexcept {
    return severity == RuntimeMaterialResolveDiagnosticSeverity::Error
        ? SceneRenderDiagnosticSeverity::Error
        : SceneRenderDiagnosticSeverity::Warning;
}

[[nodiscard]] SceneRenderDiagnosticKind ConvertKind(RuntimeMaterialResolveDiagnosticKind kind) noexcept {
    switch (kind) {
    case RuntimeMaterialResolveDiagnosticKind::MissingMaterialAsset:
        return SceneRenderDiagnosticKind::MissingMaterialAsset;
    case RuntimeMaterialResolveDiagnosticKind::UnresolvedTexturePath:
        return SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath;
    case RuntimeMaterialResolveDiagnosticKind::UnsupportedAssetType:
    case RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed:
    case RuntimeMaterialResolveDiagnosticKind::MaterialInstanceLoadFailed:
    case RuntimeMaterialResolveDiagnosticKind::MissingParentMaterial:
    case RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed:
    case RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed:
    case RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed:
    case RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed:
        return SceneRenderDiagnosticKind::InvalidMaterialAsset;
    }
    return SceneRenderDiagnosticKind::InvalidMaterialAsset;
}

void EmitRuntimeMaterialResolverDiagnostics(
    const RuntimeRenderResourceEnsureContext& context,
    const ResolvedRuntimeMaterialAsset& resolved,
    std::uint64_t materialAssetId) {
    context.materialResolverDiagnosticCount += static_cast<std::uint32_t>(resolved.diagnostics.size());
    for (const RuntimeMaterialResolveDiagnostic& diagnostic : resolved.diagnostics) {
        context.diagnostics.events.push_back(SceneRenderDiagnosticEvent{
            .severity = ConvertSeverity(diagnostic.severity),
            .kind = ConvertKind(diagnostic.kind),
            .materialAssetId = diagnostic.assetId.IsValid() ? diagnostic.assetId.value : materialAssetId,
            .instanceCount = 1U,
        });
    }
}

void EmitCachedRuntimeMaterialState(
    const RuntimeRenderResourceEnsureContext& context,
    const RuntimeMaterialResource& cached,
    std::uint64_t materialAssetId) {
    if (cached.status == RuntimeMaterialResolveStatus::DefaultMaterial) {
        ++context.defaultMaterialFallbackCount;
        ++context.materialFallbackCount;
    } else if (cached.status == RuntimeMaterialResolveStatus::ErrorMaterial) {
        ++context.errorMaterialFallbackCount;
        ++context.materialFallbackCount;
        ++context.materialErrorCount;
    } else if (cached.status == RuntimeMaterialResolveStatus::LastGoodMaterial) {
        ++context.materialErrorCount;
    }
    ResolvedRuntimeMaterialAsset resolved{};
    resolved.diagnostics = cached.diagnostics;
    EmitRuntimeMaterialResolverDiagnostics(context, resolved, materialAssetId);
}

[[nodiscard]] std::optional<std::uint64_t> CachedRuntimeMaterialContentHash(
    kb::assets::AssetManager& manager,
    kb::assets::AssetId assetId) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
    if (metadata == nullptr) {
        return assetId.value;
    }
    if (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance") {
        return metadata->contentHash;
    }
    return RuntimeMaterialResolver::MaterialRuntimeContentHash(manager, *metadata);
}

} // namespace

void RuntimeMaterialResourceEnsurer::Ensure(
    const RuntimeRenderResourceEnsureContext& context,
    RuntimeMaterialResourceMap& materials,
    RuntimeMaterialResourceMap& embeddedMaterials) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    auto ensureMaterial = [&](std::uint64_t materialAssetId) {
        if (materialAssetId == 0U) {
            return;
        }

        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = materialAssetId,
        };
        context.frameReferences.MarkMaterial(runtimeKey);

        const auto embeddedIt = embeddedMaterials.find(runtimeKey);
        if (embeddedIt != embeddedMaterials.end()) {
            if (context.sceneRenderer.Resources().ContainsMaterial(embeddedIt->second.handle)) {
                RuntimeMaterialResource& cached = embeddedIt->second;
                cached.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
                return;
            }
            context.sceneRenderer.ResourceMap().UnbindMaterialHandle(embeddedIt->second.handle);
            embeddedMaterials.erase(embeddedIt);
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            return;
        }

        const kb::assets::AssetId assetId{ materialAssetId };
        auto cacheIt = materials.find(runtimeKey);
        const std::optional<std::uint64_t> cachedContentHash = CachedRuntimeMaterialContentHash(manager, assetId);
        if (cacheIt != materials.end() &&
            cachedContentHash.has_value() &&
            cacheIt->second.contentHash == *cachedContentHash &&
            context.sceneRenderer.Resources().ContainsMaterial(cacheIt->second.handle)) {
            RuntimeMaterialResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
            EmitCachedRuntimeMaterialState(context, cached, materialAssetId);
            return;
        }

        const ResolvedRuntimeMaterialAsset resolvedAsset = context.materialResolver.ResolveAsset(manager, assetId);
        if (!resolvedAsset.resolved) {
            if (cacheIt != materials.end()) {
                context.sceneRenderer.ResourceMap().UnbindMaterialHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMaterial(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                materials.erase(cacheIt);
            }
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            return;
        }

        if (resolvedAsset.status == RuntimeMaterialResolveStatus::DefaultMaterial) {
            ++context.defaultMaterialFallbackCount;
            ++context.materialFallbackCount;
        } else if (resolvedAsset.status == RuntimeMaterialResolveStatus::ErrorMaterial) {
            ++context.materialErrorCount;
        }
        EmitRuntimeMaterialResolverDiagnostics(context, resolvedAsset, materialAssetId);

        if (resolvedAsset.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            cacheIt != materials.end() &&
            cacheIt->second.status != RuntimeMaterialResolveStatus::ErrorMaterial &&
            context.sceneRenderer.Resources().ContainsMaterial(cacheIt->second.handle)) {
            RuntimeMaterialResource& cached = cacheIt->second;
            cached.contentHash = resolvedAsset.contentHash;
            cached.lastReferencedFrame = context.currentFrame;
            cached.status = RuntimeMaterialResolveStatus::LastGoodMaterial;
            cached.diagnostics = resolvedAsset.diagnostics;
            context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
            return;
        }
        if (resolvedAsset.status == RuntimeMaterialResolveStatus::ErrorMaterial) {
            ++context.errorMaterialFallbackCount;
            ++context.materialFallbackCount;
        }

        if (cacheIt != materials.end() && cacheIt->second.contentHash == resolvedAsset.contentHash && context.sceneRenderer.Resources().ContainsMaterial(cacheIt->second.handle)) {
            RuntimeMaterialResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            cached.status = resolvedAsset.status;
            cached.diagnostics = resolvedAsset.diagnostics;
            context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
            return;
        }

        const bool reloadsExistingMaterial = cacheIt != materials.end();
        if (cacheIt != materials.end()) {
            context.sceneRenderer.ResourceMap().UnbindMaterialHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMaterial(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            materials.erase(cacheIt);
        }

        const ResolvedRuntimeMaterialDesc materialDesc = resolvedAsset.material;
        context.unresolvedMaterialTexturePathCount += materialDesc.unresolvedTexturePathCount;
        EmitUnresolvedMaterialTexturePathDiagnostic(context.diagnostics, materialAssetId, materialDesc.unresolvedTexturePathCount);
        const RenderMaterialHandle handle = context.sceneRenderer.Resources().RegisterMaterial(materialDesc.desc);
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            static_cast<void>(manager.Unload(assetId));
            return;
        }
        ++context.materialLoadedCount;
        if (reloadsExistingMaterial) {
            ++context.materialReloadCount;
        }

        materials[runtimeKey] = RuntimeMaterialResource{
            .handle = handle,
            .contentHash = resolvedAsset.contentHash,
            .lastReferencedFrame = context.currentFrame,
            .status = resolvedAsset.status,
            .diagnostics = resolvedAsset.diagnostics,
        };
        context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, handle);
    };

    for (const auto& [entityId, proxy] : context.renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        ensureMaterial(proxy.desc.materialAssetId);
        for (std::uint32_t slotIndex = 0U; slotIndex < proxy.desc.materialSlotOverrideCount && slotIndex < kMaxSceneMaterialSlotOverrides; ++slotIndex) {
            ensureMaterial(proxy.desc.materialSlotAssetIds[slotIndex]);
        }

        const RenderMeshHandle meshHandle = context.sceneRenderer.ResourceMap().ResolveMesh(proxy.desc.meshAssetId);
        const RenderMeshResource* meshResource = context.sceneRenderer.Resources().FindMesh(meshHandle);
        if (meshResource == nullptr) {
            continue;
        }
        for (const RenderMaterialSlot& slot : meshResource->materialSlots) {
            ensureMaterial(slot.defaultMaterialAssetId);
        }
    }
}

} // namespace kb::render
