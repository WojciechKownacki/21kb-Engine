#include "scene/EditorAnimationPreviewScene.hpp"

#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

namespace kb::editor {

const kb::scene::Scene& EditorAnimationPreviewScene::SceneFor(
    const kb::scene::Scene& source, const AnimationPreviewContext& context) {
    if (scene_ == nullptr || sourceSceneId_ != source.Id() || contextRevision_ != context.Revision()) {
        Rebuild(source, context);
    }
    return *scene_;
}

void EditorAnimationPreviewScene::Rebuild(
    const kb::scene::Scene& source, const AnimationPreviewContext& context) {
    scene_ = std::make_unique<kb::scene::Scene>();
    for (const kb::assets::AssetMetadata& metadata : source.Assets().Manager().Registry().All()) {
        static_cast<void>(scene_->Assets().Manager().RegisterAsset(metadata));
    }
    const kb::scene::SceneEntity entity = scene_->Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Animation Preview" });
    static_cast<void>(scene_->Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = context.SkeletalMeshAsset().value,
    }));
    static_cast<void>(scene_->Components().DeformedGeometries().Set(entity, kb::scene::DrawD3DeformedGeometryComponent{
        .skeletalMeshAssetId = context.SkeletalMeshAsset().value,
        .enabled = context.SkeletalMeshAsset().IsValid(),
    }));
    if (context.SkeletonAsset().IsValid()) {
        const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
            scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(context.SkeletonAsset());
        if (skeleton.IsLoaded()) {
            static_cast<void>(scene_->Components().SkeletonBindings().Set(entity, kb::scene::SkeletonBindingComponent{
                .skeletonAssetId = context.SkeletonAsset().value,
                .skeletonCompatibilitySignature = kb::scene::SkeletonCompatibilitySignature(*skeleton),
                .enabled = true,
            }));
        }
    }
    if (context.ControllerAsset().IsValid()) {
        static_cast<void>(scene_->Components().Animators().Set(entity, kb::scene::Animator{
            .controllerAssetId = context.ControllerAsset().value,
            .enabled = true,
        }));
    }
    static_cast<void>(scene_->Runtime().Update(0.0F));
    sourceSceneId_ = source.Id();
    contextRevision_ = context.Revision();
    ++revision_;
}

void EditorAnimationPreviewScene::Clear() noexcept {
    scene_.reset();
    sourceSceneId_ = 0U;
    contextRevision_ = 0U;
    ++revision_;
}

} // namespace kb::editor
