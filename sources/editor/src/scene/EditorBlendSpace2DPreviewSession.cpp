#include "scene/EditorBlendSpace2DPreviewSession.hpp"
#include "scene/EditorPlayModeSelectionSnapshot.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace kb::editor {
namespace {
constexpr const char* axisX = "BlendPreviewX";
constexpr const char* axisY = "BlendPreviewY";
constexpr const char* layer = "BlendPreview";
bool SampleNow(kb::scene::Scene& scene,std::string& error) {
    static_cast<void>(scene.Runtime().Update(0));
    const auto errors=scene.Runtime().DrainSceneSystemErrors();
    if (!errors.empty()) { error=errors.front();return false; }
    scene.Runtime().SynchronizeTransforms();return true;
}
}

bool EditorBlendSpace2DPreviewSession::Open(kb::scene::Scene& source, kb::scene::SceneEntity owner,
    const kb::project::ProjectDescriptor& project, kb::assets::AssetId asset,
    const kb::scene::BlendSpace2DAsset& working, float x, float y, float phase, std::string& error) {
    error.clear();
    if (!source.Entities().IsAlive(owner) || !asset.IsValid()) { error="Select a live BlendSpace2D preview owner"; return false; }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(phase) || phase<0 || phase>1) {
        error="Invalid BlendSpace2D preview cursor"; return false;
    }
    try {
        if (working.samples.empty() || !kb::scene::BlendSpace2DPlan::Build(working,error))
            throw std::runtime_error(error.empty()?"BlendSpace2D has no samples":error);
        auto candidate=std::make_unique<kb::scene::Scene>(project,kb::scene::SceneMode::Runtime);
        auto& manager=candidate->Assets().Manager();
        manager.Mounts()=source.Assets().Manager().Mounts();
        for (const auto& metadata : source.Assets().Manager().Registry().All()) static_cast<void>(manager.RegisterAsset(metadata));
        if (!manager.PublishRuntimeAsset(asset,std::make_shared<kb::scene::BlendSpace2DAsset>(working)))
            throw std::runtime_error("BlendSpace2D working copy could not be published to preview");
        EditorPlayModeSelectionSnapshot hierarchy;
        hierarchy.CaptureAuthoredHierarchy(source);
        if (!kb::scene::SceneDocumentService::LoadIntoScene(*candidate,kb::scene::SceneDocumentService::Capture(source,"BlendSpace2D Preview")))
            throw std::runtime_error("BlendSpace2D preview scene could not be copied");
        kb::scene::SceneEntity cloned{};
        for (const auto& mapping : hierarchy.RecreatedEntities(*candidate)) if (mapping.from==owner) cloned=mapping.to;
        if (!candidate->Entities().IsAlive(cloned)) throw std::runtime_error("BlendSpace2D preview owner could not be remapped");
        kb::assets::AssetId controllerId{0x21B2D00000000001ULL};
        while (manager.Registry().Find(controllerId)) {
            if (controllerId.value==std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("No transient preview asset identity is available");
            ++controllerId.value;
        }
        kb::assets::AssetMetadata metadata;
        metadata.id=controllerId; metadata.type=kb::scene::kAnimatorControllerAssetType;
        metadata.name="BlendSpace2D Preview Controller";
        metadata.virtualPath="/__Preview/BlendSpace2D.kbanimcontroller";
        if (!manager.RegisterAsset(metadata)) throw std::runtime_error("Preview controller could not be registered");
        auto controller=std::make_shared<kb::scene::AnimatorController>();
        controller->parameters={{.name=axisX,.floatDefault=x},{.name=axisY,.floatDefault=y}};
        kb::scene::AnimatorControllerLayer definition{.name=layer,.defaultState="Motion"};
        definition.states.push_back({.id=1,.name="Motion",.blendParameter=axisX,
            .blendSpaceReference=kb::assets::ToString(asset),.blendParameterY=axisY});
        controller->layers.push_back(std::move(definition));
        if (!manager.PublishRuntimeAsset(controllerId,std::move(controller)))
            throw std::runtime_error("Preview controller could not be assigned");
        candidate->Components().Animators().Set(cloned,{.controllerAssetId=controllerId.value});
        // Authoring previews drive the transient controller directly. Project
        // behaviours are not installed into this isolated scene copy.
        candidate->Runtime().SetPlaying(true);
        static_cast<void>(candidate->Runtime().Update(0));
        const auto errors=candidate->Runtime().DrainSceneSystemErrors();
        if (!errors.empty()) throw std::runtime_error(errors.front());
        if (candidate->Animators().Controller(cloned)!=controllerId.value || !candidate->Animators().SeekNormalized(cloned,phase))
            throw std::runtime_error("BlendSpace2D preview clips could not be sampled on the selected owner");
        if (!SampleNow(*candidate,error)) throw std::runtime_error(error);
        Clear();
        scene_=std::move(candidate);owner_=cloned;
        x_=x;y_=y;phase_=phase;
        return true;
    } catch (const std::exception& failure) { error=failure.what(); return false; }
}

bool EditorBlendSpace2DPreviewSession::SetCoordinates(float x, float y, std::string& error) {
    error.clear();
    if (!scene_ || !std::isfinite(x) || !std::isfinite(y)) { error="Select a preview owner and finite coordinates"; return false; }
    if (!scene_->Animators().SetFloat(owner_,axisX,x) || !scene_->Animators().SetFloat(owner_,axisY,y) ||
        !scene_->Animators().SeekNormalized(owner_,phase_) || !SampleNow(*scene_,error)) {
        static_cast<void>(scene_->Animators().SetFloat(owner_,axisX,x_));
        static_cast<void>(scene_->Animators().SetFloat(owner_,axisY,y_));
        error="BlendSpace2D preview coordinates could not be sampled";return false;
    }
    x_=x;y_=y;
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

bool EditorBlendSpace2DPreviewSession::Seek(float phase,std::string& error) {
    error.clear();
    if (!scene_ || !std::isfinite(phase) || phase<0 || phase>1 || !scene_->Animators().SeekNormalized(owner_,phase) || !SampleNow(*scene_,error)) {
        error="BlendSpace2D preview phase could not be sampled";return false;
    }
    phase_=phase;
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

bool EditorBlendSpace2DPreviewSession::Tick(float delta,std::string& error) {
    error.clear();
    if (!std::isfinite(delta) || delta<0) { error="BlendSpace2D preview delta must be finite and nonnegative";return false; }
    if (!scene_ || !playing_) return false;
    static_cast<void>(scene_->Runtime().Update(delta));
    const auto errors=scene_->Runtime().DrainSceneSystemErrors();
    if (!errors.empty()) { error=errors.front();playing_=false;return false; }
    if (const auto state=scene_->Animators().State(owner_,layer)) phase_=state->normalizedTime;
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

void EditorBlendSpace2DPreviewSession::Clear() noexcept {
    if (scene_ && release_) release_(*scene_);
    scene_.reset();owner_={};playing_=false;phase_=0;
}
} // namespace kb::editor
