#include "assets/EditorAssetOpenPolicy.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

} // namespace

bool EditorAssetOpenPolicy::IsSceneDocument(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "Scene" && Lower(metadata.virtualPath.extension().string()) == ".21kbscene";
}

bool EditorAssetOpenPolicy::CanOpen(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "LuaScript" || ProjectFilesAssetIconResolver::IsSkeletalMesh(metadata) ||
        ProjectFilesAssetIconResolver::IsSkeleton(metadata) || metadata.type == kb::scene::kAnimationClipAssetType ||
        metadata.type == kb::scene::kAnimatorControllerAssetType ||
        metadata.type == kb::scene::kParticleEffectAssetType || metadata.type == kb::scene::kTimelineAssetType ||
        metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance" ||
        metadata.type == kb::render::kRenderMaterialGraphAssetType || IsSceneDocument(metadata);
}

} // namespace kb::editor
