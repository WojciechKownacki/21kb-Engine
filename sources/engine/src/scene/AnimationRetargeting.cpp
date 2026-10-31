#include "engine/scene/AnimationRetargeting.hpp"

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool IsAlpha(char value) noexcept { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); }
[[nodiscard]] bool IsDigit(char value) noexcept { return value >= '0' && value <= '9'; }
[[nodiscard]] bool IsUpper(char value) noexcept { return value >= 'A' && value <= 'Z'; }
[[nodiscard]] char Lower(char value) noexcept { return IsUpper(value) ? static_cast<char>(value - 'A' + 'a') : value; }

// "mixamorig:LeftUpLeg" -> left up leg, "upperarm_l" -> upperarm l, "DEF-forearm.L.001" -> def forearm l 001.
[[nodiscard]] std::vector<std::string> Tokens(std::string_view name) {
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&]() {
        if (!current.empty()) tokens.push_back(std::move(current));
        current.clear();
    };
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char value = name[index];
        if (!IsAlpha(value) && !IsDigit(value)) {
            flush();
            continue;
        }
        if (!current.empty()) {
            const char previous = name[index - 1U];
            const bool camel = IsUpper(value) && !IsUpper(previous) && IsAlpha(previous);
            const bool digitEdge = IsDigit(value) != IsDigit(previous);
            if (camel || digitEdge) flush();
        }
        current.push_back(Lower(value));
    }
    flush();
    return tokens;
}

[[nodiscard]] std::string_view Synonym(std::string_view core) noexcept {
    struct Entry { std::string_view from; std::string_view to; };
    static constexpr std::array<Entry, 30> kSynonyms{ {
        { "pelvis", "hips" }, { "hip", "hips" }, { "hips", "hips" },
        { "clavicle", "clavicle" }, { "shoulder", "clavicle" }, { "collar", "clavicle" },
        { "upperarm", "upperarm" }, { "arm", "upperarm" },
        { "forearm", "forearm" }, { "lowerarm", "forearm" }, { "elbow", "forearm" },
        { "hand", "hand" }, { "wrist", "hand" },
        { "thigh", "thigh" }, { "upleg", "thigh" }, { "upperleg", "thigh" },
        { "calf", "calf" }, { "shin", "calf" }, { "leg", "calf" }, { "lowerleg", "calf" }, { "knee", "calf" },
        { "foot", "foot" }, { "ankle", "foot" },
        { "toe", "toe" }, { "toes", "toe" }, { "toebase", "toe" }, { "ball", "toe" },
        { "neck", "neck" }, { "head", "head" }, { "chest", "chest" },
    } };
    for (const Entry& entry : kSynonyms) {
        if (entry.from == core) return entry.to;
    }
    return core;
}

[[nodiscard]] Vec3 Multiply(Vec3 lhs, Vec3 rhs) noexcept { return { lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z }; }
[[nodiscard]] Vec3 Divide(Vec3 lhs, Vec3 rhs) noexcept {
    const auto safe = [](float value) { return std::abs(value) > 1.0e-8F ? value : 1.0F; };
    return { lhs.x / safe(rhs.x), lhs.y / safe(rhs.y), lhs.z / safe(rhs.z) };
}
[[nodiscard]] Vec3 Scale(Vec3 value, float factor) noexcept { return { value.x * factor, value.y * factor, value.z * factor }; }

// Character-space transforms from local ones; parents come before children (ValidateSkeletonAsset).
void ToGlobal(const SkeletonAsset& skeleton, std::span<const LocalTransform> local, std::vector<LocalTransform>& global) {
    global.resize(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const std::int32_t parent = skeleton.bones[index].parentIndex;
        if (parent < 0) {
            global[index] = local[index];
            continue;
        }
        const LocalTransform& up = global[static_cast<std::size_t>(parent)];
        global[index] = {
            .position = up.position + kb::math::Rotate(up.rotation, Multiply(up.scale, local[index].position)),
            .rotation = kb::math::Normalize(up.rotation * local[index].rotation),
            .scale = Multiply(up.scale, local[index].scale),
        };
    }
}

[[nodiscard]] LocalTransform SampleTrack(const AnimationBoneTrack& track, float time) {
    const std::vector<AnimationBoneKeyframe>& keys = track.keyframes;
    if (time <= keys.front().timeSeconds) return keys.front().transform;
    if (time >= keys.back().timeSeconds) return keys.back().transform;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
        [](float value, const AnimationBoneKeyframe& key) { return value < key.timeSeconds; });
    const AnimationBoneKeyframe& b = *upper;
    const AnimationBoneKeyframe& a = *(upper - 1);
    const float alpha = (time - a.timeSeconds) / (b.timeSeconds - a.timeSeconds);
    const auto lerp = [alpha](Vec3 x, Vec3 y) {
        return Vec3{ kb::math::Lerp(x.x, y.x, alpha), kb::math::Lerp(x.y, y.y, alpha), kb::math::Lerp(x.z, y.z, alpha) };
    };
    return { .position = lerp(a.transform.position, b.transform.position),
             .rotation = kb::math::Slerp(a.transform.rotation, b.transform.rotation, alpha),
             .scale = lerp(a.transform.scale, b.transform.scale) };
}

[[nodiscard]] bool Near(const LocalTransform& lhs, const LocalTransform& rhs) noexcept {
    constexpr float kEpsilon = 1.0e-5F;
    const auto near = [](float a, float b) { return std::abs(a - b) <= kEpsilon; };
    const float dot = lhs.rotation.x * rhs.rotation.x + lhs.rotation.y * rhs.rotation.y + lhs.rotation.z * rhs.rotation.z +
        lhs.rotation.w * rhs.rotation.w;
    return near(lhs.position.x, rhs.position.x) && near(lhs.position.y, rhs.position.y) && near(lhs.position.z, rhs.position.z) &&
        std::abs(dot) >= 1.0F - kEpsilon && near(lhs.scale.x, rhs.scale.x) && near(lhs.scale.y, rhs.scale.y) &&
        near(lhs.scale.z, rhs.scale.z);
}

[[nodiscard]] std::vector<LocalTransform> ReferenceLocals(const SkeletonAsset& skeleton) {
    std::vector<LocalTransform> locals;
    locals.reserve(skeleton.bones.size());
    for (const SkeletonBone& bone : skeleton.bones) locals.push_back(bone.referencePose);
    return locals;
}

[[nodiscard]] float Height(std::span<const LocalTransform> global) noexcept {
    if (global.empty()) return 0.0F;
    float low = global.front().position.y;
    float high = low;
    for (const LocalTransform& transform : global) {
        low = std::min(low, transform.position.y);
        high = std::max(high, transform.position.y);
    }
    return high - low;
}

} // namespace

std::string CanonicalBoneName(std::string_view name) {
    const std::size_t separator = name.find_last_of(":|");
    if (separator != std::string_view::npos && separator + 1U < name.size()) name.remove_prefix(separator + 1U);
    std::vector<std::string> tokens = Tokens(name);
    // Prefixes rigs put on every bone carry no meaning ("DEF-", "Bip01 ", "mixamorig").
    static constexpr std::array<std::string_view, 12> kNoise{
        "def", "org", "mch", "bip", "jnt", "joint", "bn", "rig", "armature", "mixamorig", "cc", "base" };
    std::erase_if(tokens, [](const std::string& token) { return std::ranges::find(kNoise, token) != kNoise.end(); });
    char side = 0;
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        if (*it == "l" || *it == "left") side = 'l';
        else if (*it == "r" || *it == "right") side = 'r';
        else continue;
        tokens.erase(it);
        break;
    }
    std::string core;
    for (std::string& token : tokens) {
        if (IsDigit(token.front())) {
            // "01" and "1" are one number; a lone 0 is dropped ("spine0" is "spine").
            const std::size_t first = token.find_first_not_of('0');
            token = first == std::string::npos ? std::string{} : token.substr(first);
        }
        core += token;
    }
    // A number right after a word ("Bip01") leaves its digits at the front: they name no part.
    while (!core.empty() && IsDigit(core.front())) core.erase(core.begin());
    std::string canonical{ Synonym(core) };
    return side == 0 ? canonical : std::string{ side } + ":" + canonical;
}

namespace {

// The chain a bone belongs to: its role without the number. The spine's top is the chest in some
// rigs and Spine2 in others, so a chest counts as the last link of the spine.
[[nodiscard]] std::string ChainKey(std::string_view name) {
    std::string key = CanonicalBoneName(name);
    std::erase_if(key, [](char value) { return IsDigit(value); });
    return key == "chest" ? std::string{ "spine" } : key;
}

// Spine and neck links are numbered differently by every tool (Mixamo Spine, Spine1 against
// Unreal spine_01, spine_02): their numbers mean nothing across rigs, only their order does.
[[nodiscard]] bool OrderedChain(std::string_view key) noexcept { return key == "spine" || key == "neck"; }

} // namespace

SkeletonBoneMapping MapSkeletonBones(const SkeletonAsset& source, const SkeletonAsset& target) {
    SkeletonBoneMapping mapping;
    mapping.sourceOfTarget.assign(target.bones.size(), -1);
    std::unordered_map<std::string, std::int32_t> exact;
    std::unordered_map<std::string, std::int32_t> canonical;
    for (std::size_t index = 0U; index < source.bones.size(); ++index) {
        exact.emplace(source.bones[index].name, static_cast<std::int32_t>(index));
        const auto [it, inserted] = canonical.emplace(CanonicalBoneName(source.bones[index].name), static_cast<std::int32_t>(index));
        if (!inserted) it->second = -2;
    }
    std::vector<bool> used(source.bones.size(), false);
    for (std::size_t index = 0U; index < target.bones.size(); ++index) {
        std::int32_t found = -1;
        if (const auto it = exact.find(target.bones[index].name); it != exact.end()) {
            found = it->second;
        } else if (const auto folded = canonical.find(CanonicalBoneName(target.bones[index].name));
                   folded != canonical.end() && folded->second >= 0 && !OrderedChain(ChainKey(target.bones[index].name))) {
            found = folded->second;
        }
        mapping.sourceOfTarget[index] = found;
        if (found >= 0) used[static_cast<std::size_t>(found)] = true;
    }

    // The bones still unmatched in a chain (a numbered finger, the spine, the neck) pair up in
    // order, root first.
    const auto chainOf = [](const SkeletonAsset& skeleton) {
        std::unordered_map<std::string, std::vector<std::size_t>> chains;
        for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
            chains[ChainKey(skeleton.bones[index].name)].push_back(index);
        }
        return chains;
    };
    const auto sourceChains = chainOf(source);
    for (const auto& [key, targetBones] : chainOf(target)) {
        const auto sourceBones = sourceChains.find(key);
        if (sourceBones == sourceChains.end()) continue;
        std::vector<std::size_t> freeSource;
        for (const std::size_t bone : sourceBones->second) {
            if (!used[bone]) freeSource.push_back(bone);
        }
        std::size_t next = 0U;
        for (const std::size_t bone : targetBones) {
            if (mapping.sourceOfTarget[bone] >= 0 || next >= freeSource.size()) continue;
            mapping.sourceOfTarget[bone] = static_cast<std::int32_t>(freeSource[next]);
            used[freeSource[next++]] = true;
        }
    }
    for (const std::int32_t found : mapping.sourceOfTarget) {
        if (found >= 0) ++mapping.mappedCount;
    }
    return mapping;
}

static std::optional<AnimationClip> RetargetAnimationClipImpl(
    const AnimationClip& clip, const SkeletonAsset& source,
    const SkeletonAsset& target, std::uint64_t targetSkeletonAssetId, std::uint64_t targetCompatibilitySignature,
    AnimationRetargetReport* report, std::string* error, const AnimationRetargetProfile* profile) {
    const auto fail = [error](std::string message) -> std::optional<AnimationClip> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (!ValidateSkeletonAsset(source).valid || !ValidateSkeletonAsset(target).valid) return fail("a skeleton is not valid");
    SkeletonBoneMapping mapping;
    if (profile != nullptr) {
        if (profile->sourceSignature != SkeletonCompatibilitySignature(source) ||
            profile->targetSignature != SkeletonCompatibilitySignature(target) ||
            profile->sourceOfTarget.size() != target.bones.size())
            return fail("retarget profile does not match the source and target skeletons");
        mapping.sourceOfTarget = profile->sourceOfTarget;
        for (const auto index : mapping.sourceOfTarget) {
            if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= source.bones.size()))
                return fail("retarget profile contains an invalid source bone index");
            if (index >= 0) ++mapping.mappedCount;
        }
    } else {
        mapping = MapSkeletonBones(source, target);
    }

    std::vector<const AnimationBoneTrack*> sourceTrack(source.bones.size(), nullptr);
    std::unordered_map<SkeletonBoneId, std::size_t> sourceIndex;
    for (std::size_t index = 0U; index < source.bones.size(); ++index) sourceIndex.emplace(source.bones[index].id, index);
    for (const AnimationBoneTrack& track : clip.skeletalTracks) {
        const auto found = sourceIndex.find(track.boneId);
        if (found == sourceIndex.end() || track.keyframes.empty()) return fail("the clip animates a bone its skeleton does not have");
        sourceTrack[found->second] = &track;
    }
    std::vector<bool> followed(source.bones.size(), false);
    for (const std::int32_t mapped : mapping.sourceOfTarget) {
        if (mapped >= 0) followed[static_cast<std::size_t>(mapped)] = true;
    }
    AnimationRetargetReport result{ .mappedBones = mapping.mappedCount, .targetBones = target.bones.size() };
    std::size_t animated = 0U;
    std::size_t animatedFollowed = 0U;
    for (std::size_t index = 0U; index < source.bones.size(); ++index) {
        if (sourceTrack[index] == nullptr) continue;
        ++animated;
        if (followed[index]) ++animatedFollowed;
        else result.unmappedAnimatedBones.push_back(source.bones[index].name);
    }
    if (animated > 0U && animatedFollowed * 2U < animated) {
        if (report != nullptr) *report = result;
        return fail("only " + std::to_string(animatedFollowed) + " of the " + std::to_string(animated) +
            " animated bones have a bone of the same name (or role) on the character");
    }

    // Rest poses in character space.
    const std::vector<LocalTransform> sourceRestLocal = ReferenceLocals(source);
    const std::vector<LocalTransform> targetRestLocal = ReferenceLocals(target);
    std::vector<LocalTransform> sourceRest;
    std::vector<LocalTransform> targetRest;
    ToGlobal(source, sourceRestLocal, sourceRest);
    ToGlobal(target, targetRestLocal, targetRest);

    // The hips carry the body's travel; the bones above them (a root) travel with them.
    std::int32_t hips = -1;
    for (std::size_t index = 0U; index < target.bones.size(); ++index) {
        if (mapping.sourceOfTarget[index] >= 0 && CanonicalBoneName(target.bones[index].name) == "hips") {
            hips = static_cast<std::int32_t>(index);
            break;
        }
    }
    if (hips < 0) {
        for (std::size_t index = 0U; index < target.bones.size() && hips < 0; ++index) {
            if (mapping.sourceOfTarget[index] >= 0) hips = static_cast<std::int32_t>(index);
        }
    }
    std::vector<bool> travels(target.bones.size(), false);
    for (std::int32_t bone = hips; bone >= 0; bone = target.bones[static_cast<std::size_t>(bone)].parentIndex) {
        travels[static_cast<std::size_t>(bone)] = mapping.sourceOfTarget[static_cast<std::size_t>(bone)] >= 0;
    }
    float scale = 1.0F;
    if (hips >= 0) {
        const float targetHeight = targetRest[static_cast<std::size_t>(hips)].position.y;
        const float sourceHeight = sourceRest[static_cast<std::size_t>(mapping.sourceOfTarget[static_cast<std::size_t>(hips)])].position.y;
        if (targetHeight > 1.0e-4F && sourceHeight > 1.0e-4F) {
            scale = targetHeight / sourceHeight;
        } else if (Height(sourceRest) > 1.0e-4F && Height(targetRest) > 1.0e-4F) {
            scale = Height(targetRest) / Height(sourceRest);
        }
    }
    result.translationScale = scale;

    // Every instant a source key sits on (resampled at 60 Hz when there are very many).
    std::vector<float> times;
    for (const AnimationBoneTrack& track : clip.skeletalTracks) {
        for (const AnimationBoneKeyframe& key : track.keyframes) times.push_back(key.timeSeconds);
    }
    times.push_back(0.0F);
    times.push_back(clip.durationSeconds);
    std::ranges::sort(times);
    times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::abs(a - b) < 1.0e-6F; }), times.end());
    if (times.size() > 4096U) {
        const std::size_t count = static_cast<std::size_t>(std::ceil(clip.durationSeconds * 60.0F)) + 1U;
        times.clear();
        for (std::size_t index = 0U; index < count; ++index) {
            times.push_back(std::min(clip.durationSeconds, static_cast<float>(index) / 60.0F));
        }
    }

    // Layer masks follow the nearest animated source bone (the bone itself, else its parents).
    std::vector<std::uint64_t> maskOf(source.bones.size(), ~std::uint64_t{ 0U });
    for (std::size_t index = 0U; index < source.bones.size(); ++index) {
        for (std::int32_t bone = static_cast<std::int32_t>(index); bone >= 0; bone = source.bones[static_cast<std::size_t>(bone)].parentIndex) {
            if (const AnimationBoneTrack* track = sourceTrack[static_cast<std::size_t>(bone)]) {
                maskOf[index] = track->bindingMask;
                break;
            }
        }
    }

    std::vector<AnimationBoneTrack> tracks(target.bones.size());
    std::vector<LocalTransform> sourceLocal(source.bones.size());
    std::vector<LocalTransform> sourcePose;
    std::vector<LocalTransform> targetLocal(target.bones.size());
    std::vector<LocalTransform> targetPose(target.bones.size());
    for (const float time : times) {
        for (std::size_t index = 0U; index < source.bones.size(); ++index) {
            sourceLocal[index] = sourceTrack[index] != nullptr ? SampleTrack(*sourceTrack[index], time) : sourceRestLocal[index];
        }
        ToGlobal(source, sourceLocal, sourcePose);
        for (std::size_t index = 0U; index < target.bones.size(); ++index) {
            const std::int32_t parent = target.bones[index].parentIndex;
            const LocalTransform up = parent < 0 ? LocalTransform{} : targetPose[static_cast<std::size_t>(parent)];
            const Quat inverseUp = kb::math::Conjugate(kb::math::Normalize(up.rotation));
            LocalTransform local = targetRestLocal[index];
            const std::int32_t mapped = mapping.sourceOfTarget[index];
            if (mapped >= 0) {
                const std::size_t from = static_cast<std::size_t>(mapped);
                // The source bone's turn away from its rest pose, applied to the target bone's rest pose.
                const Quat global = kb::math::Normalize(sourcePose[from].rotation *
                    kb::math::Conjugate(kb::math::Normalize(sourceRest[from].rotation)) * targetRest[index].rotation);
                local.rotation = kb::math::Normalize(inverseUp * global);
                if (travels[index]) {
                    const Vec3 position = targetRest[index].position + Scale(sourcePose[from].position - sourceRest[from].position, scale);
                    local.position = Divide(kb::math::Rotate(inverseUp, position - up.position), up.scale);
                }
            }
            targetLocal[index] = local;
            targetPose[index] = {
                .position = up.position + kb::math::Rotate(up.rotation, Multiply(up.scale, local.position)),
                .rotation = kb::math::Normalize(up.rotation * local.rotation),
                .scale = Multiply(up.scale, local.scale),
            };
            if (mapped >= 0) tracks[index].keyframes.push_back({ .timeSeconds = time, .transform = local });
        }
    }

    AnimationClip retargeted = clip;
    retargeted.targetSkeletonAssetId = targetSkeletonAssetId;
    retargeted.targetSkeletonCompatibilitySignature = targetCompatibilitySignature;
    retargeted.skeletalTracks.clear();
    for (std::size_t index = 0U; index < target.bones.size(); ++index) {
        const std::int32_t mapped = mapping.sourceOfTarget[index];
        if (mapped < 0) continue;
        AnimationBoneTrack& track = tracks[index];
        // A bone that never leaves its rest pose needs no track.
        const bool still = std::ranges::all_of(track.keyframes,
            [&](const AnimationBoneKeyframe& key) { return Near(key.transform, targetRestLocal[index]); });
        const bool rootMotionBone = clip.rootMotionMode == AnimationRootMotionMode::ExtractFromBone &&
            source.bones[static_cast<std::size_t>(mapped)].id == clip.rootMotionBoneId;
        if (still && !rootMotionBone) continue;
        track.boneId = target.bones[index].id;
        track.bindingMask = maskOf[static_cast<std::size_t>(mapped)];
        retargeted.skeletalTracks.push_back(std::move(track));
    }
    if (clip.rootMotionMode == AnimationRootMotionMode::ExtractFromBone) {
        retargeted.rootMotionMode = AnimationRootMotionMode::None;
        retargeted.rootMotionBoneId = 0U;
        for (std::size_t index = 0U; index < target.bones.size(); ++index) {
            const std::int32_t mapped = mapping.sourceOfTarget[index];
            if (mapped >= 0 && source.bones[static_cast<std::size_t>(mapped)].id == clip.rootMotionBoneId) {
                retargeted.rootMotionMode = AnimationRootMotionMode::ExtractFromBone;
                retargeted.rootMotionBoneId = target.bones[index].id;
            }
        }
    }
    if (report != nullptr) *report = std::move(result);
    return retargeted;
}

AnimationRetargetProfile MakeAnimationRetargetProfile(const SkeletonAsset& source, const SkeletonAsset& target) {
    return {SkeletonCompatibilitySignature(source), SkeletonCompatibilitySignature(target),
        MapSkeletonBones(source, target).sourceOfTarget};
}

std::string SerializeAnimationRetargetProfile(const AnimationRetargetProfile& profile) {
    std::ostringstream output;
    output << "21kb.retarget 1\n" << profile.sourceSignature << ' ' << profile.targetSignature << ' '
        << profile.sourceOfTarget.size() << '\n';
    for (const auto index : profile.sourceOfTarget) output << index << '\n';
    return output.str();
}

std::optional<AnimationRetargetProfile> ParseAnimationRetargetProfile(std::string_view text) {
    if (text.size() > 1024U * 1024U) return std::nullopt;
    std::istringstream input{std::string{text}};
    std::string magic;
    unsigned version = 0U;
    std::size_t count = 0U;
    AnimationRetargetProfile profile;
    if (!(input >> magic >> version >> profile.sourceSignature >> profile.targetSignature >> count) ||
        magic != "21kb.retarget" || version != 1U || profile.sourceSignature == 0U ||
        profile.targetSignature == 0U || count == 0U || count > 65536U) return std::nullopt;
    profile.sourceOfTarget.resize(count);
    for (auto& index : profile.sourceOfTarget) if (!(input >> index) || index < -1) return std::nullopt;
    input >> std::ws;
    if (!input.eof()) return std::nullopt;
    return profile;
}

std::optional<AnimationClip> RetargetAnimationClip(
    const AnimationClip& clip, const SkeletonAsset& source, const SkeletonAsset& target,
    std::uint64_t targetSkeletonAssetId, std::uint64_t targetCompatibilitySignature,
    AnimationRetargetReport* report, std::string* error) {
    return RetargetAnimationClipImpl(clip, source, target, targetSkeletonAssetId,
        targetCompatibilitySignature, report, error, nullptr);
}

std::optional<AnimationClip> RetargetAnimationClip(
    const AnimationClip& clip, const SkeletonAsset& source, const SkeletonAsset& target,
    std::uint64_t targetSkeletonAssetId, std::uint64_t targetCompatibilitySignature,
    const AnimationRetargetProfile& profile, AnimationRetargetReport* report, std::string* error) {
    return RetargetAnimationClipImpl(clip, source, target, targetSkeletonAssetId,
        targetCompatibilitySignature, report, error, &profile);
}

} // namespace kb::scene
