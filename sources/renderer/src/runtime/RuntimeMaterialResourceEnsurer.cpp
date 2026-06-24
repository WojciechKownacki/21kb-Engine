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
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = materials.find(runtimeKey);
        if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
            if (cacheIt != materials.end()) {
                context.sceneRenderer.ResourceMap().UnbindMaterialHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMaterial(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                materials.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            }
            return;
        }

        const ResolvedRuntimeMaterialAsset resolvedAsset = context.materialResolver.ResolveAsset(manager, *metadata);
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

        if (cacheIt != materials.end() && cacheIt->second.contentHash == resolvedAsset.contentHash && context.sceneRenderer.Resources().ContainsMaterial(cacheIt->second.handle)) {
            RuntimeMaterialResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
            return;
        }

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

        materials[runtimeKey] = RuntimeMaterialResource{
            .handle = handle,
            .contentHash = resolvedAsset.contentHash,
            .lastReferencedFrame = context.currentFrame,
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
