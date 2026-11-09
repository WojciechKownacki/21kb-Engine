#pragma once
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
struct Rig2DPose {
    std::array<float,2> position{};
    float rotationDegrees=0;
    std::array<float,2> scale{1,1};
};
struct Rig2DBone {std::string name;std::int32_t parent=-1;Rig2DPose bind;};
struct Rig2DVertex {
    std::array<float,2> position{},uv{};
    std::array<std::uint16_t,4> bones{};
    std::array<float,4> weights{1,0,0,0};
};
struct Rig2DKey {float time=0;Rig2DPose pose;};
struct Rig2DTrack {std::uint16_t bone=0;std::vector<Rig2DKey> keys;};
struct Rig2DAsset {
    std::vector<Rig2DBone> bones;
    std::vector<Rig2DVertex> vertices;
    std::vector<std::uint32_t> indices;
    float duration=1;
    bool looping=true;
    std::vector<Rig2DTrack> tracks;
};
struct CompiledRig2D {
    SkeletonAsset skeleton;
    SkeletalMeshAsset mesh;
    AnimationClip clip;
    AnimatorController controller;
};
[[nodiscard]] std::string ValidateRig2D(const Rig2DAsset&);
[[nodiscard]] bool DecodeRig2D(std::string_view,Rig2DAsset&,std::string& error);
[[nodiscard]] std::string EncodeRig2D(const Rig2DAsset&);
[[nodiscard]] bool SaveRig2D(const std::filesystem::path&,const Rig2DAsset&,std::string& error);
// Emits the existing shared asset types. Clip reference is the virtual path of the
// resulting .kbanim; stable skeleton identity is supplied by asset discovery/path.
[[nodiscard]] bool CompileRig2D(const Rig2DAsset&,std::uint64_t skeletonAssetId,
    std::uint64_t materialAssetId,std::string_view clipReference,CompiledRig2D&,std::string& error);
// Installs canonical components. Animation playback, seeking and blending use SceneAnimators.
[[nodiscard]] bool AttachRig2D(Scene&,SceneEntity,std::string_view mesh,std::string_view controller,std::string& error);
}
