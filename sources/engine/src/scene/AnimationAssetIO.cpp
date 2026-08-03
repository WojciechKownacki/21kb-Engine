#include "engine/scene/AnimationAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/VersionedTextAssetHeader.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace kb::scene {
namespace {

std::optional<std::string> ReadText(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) return std::nullopt;
    return std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

bool WriteText(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(
        path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

bool EndOfRecord(std::istringstream& input) {
    input >> std::ws;
    return input.eof() || input.peek() == '#';
}

bool ParseBool(std::string_view text, bool& value) {
    if (text == "1" || text == "true") { value = true; return true; }
    if (text == "0" || text == "false") { value = false; return true; }
    return false;
}

const char* ParameterTypeName(AnimatorParameterType type) {
    switch (type) {
    case AnimatorParameterType::Bool: return "Bool";
    case AnimatorParameterType::Int: return "Int";
    case AnimatorParameterType::Float: return "Float";
    case AnimatorParameterType::Trigger: return "Trigger";
    }
    return "";
}

bool ParseParameterType(std::string_view text, AnimatorParameterType& type) {
    if (text == "Bool") type = AnimatorParameterType::Bool;
    else if (text == "Int") type = AnimatorParameterType::Int;
    else if (text == "Float") type = AnimatorParameterType::Float;
    else if (text == "Trigger") type = AnimatorParameterType::Trigger;
    else return false;
    return true;
}

const char* ConditionModeName(AnimatorConditionMode mode) {
    switch (mode) {
    case AnimatorConditionMode::BoolEquals: return "BoolEquals";
    case AnimatorConditionMode::IntEquals: return "IntEquals";
    case AnimatorConditionMode::IntGreater: return "IntGreater";
    case AnimatorConditionMode::IntLess: return "IntLess";
    case AnimatorConditionMode::FloatGreater: return "FloatGreater";
    case AnimatorConditionMode::FloatLess: return "FloatLess";
    case AnimatorConditionMode::TriggerSet: return "TriggerSet";
    }
    return "";
}

bool ParseConditionMode(std::string_view text, AnimatorConditionMode& mode) {
    if (text == "BoolEquals") mode = AnimatorConditionMode::BoolEquals;
    else if (text == "IntEquals") mode = AnimatorConditionMode::IntEquals;
    else if (text == "IntGreater") mode = AnimatorConditionMode::IntGreater;
    else if (text == "IntLess") mode = AnimatorConditionMode::IntLess;
    else if (text == "FloatGreater") mode = AnimatorConditionMode::FloatGreater;
    else if (text == "FloatLess") mode = AnimatorConditionMode::FloatLess;
    else if (text == "TriggerSet") mode = AnimatorConditionMode::TriggerSet;
    else return false;
    return true;
}

const char* RigConstraintTypeName(AnimatorRigConstraintType type) {
    switch (type) {
    case AnimatorRigConstraintType::TwoBoneIK: return "TwoBoneIK";
    case AnimatorRigConstraintType::Aim: return "Aim";
    case AnimatorRigConstraintType::CopyTransform: return "CopyTransform";
    }
    return "";
}

bool ParseRigConstraintType(std::string_view text, AnimatorRigConstraintType& type) {
    if (text == "TwoBoneIK") type = AnimatorRigConstraintType::TwoBoneIK;
    else if (text == "Aim") type = AnimatorRigConstraintType::Aim;
    else if (text == "CopyTransform") type = AnimatorRigConstraintType::CopyTransform;
    else return false;
    return true;
}

AnimatorParameterType ConditionParameterType(AnimatorConditionMode mode) {
    switch (mode) {
    case AnimatorConditionMode::BoolEquals: return AnimatorParameterType::Bool;
    case AnimatorConditionMode::IntEquals:
    case AnimatorConditionMode::IntGreater:
    case AnimatorConditionMode::IntLess: return AnimatorParameterType::Int;
    case AnimatorConditionMode::FloatGreater:
    case AnimatorConditionMode::FloatLess: return AnimatorParameterType::Float;
    case AnimatorConditionMode::TriggerSet: return AnimatorParameterType::Trigger;
    }
    return AnimatorParameterType::Float;
}

bool ValidateClip(AnimationClip& clip, std::string* error = nullptr) {
    const auto setError = [error](const char* message) {
        if (error != nullptr) *error = message;
    };
    setError("Animation clip has an invalid duration.");
    if (!std::isfinite(clip.durationSeconds) || clip.durationSeconds <= 0.0F) return false;
    setError("Animation clip has invalid generic transform bindings or keyframes.");
    std::unordered_set<std::string> paths;
    for (AnimationTransformTrack& track : clip.tracks) {
        if (track.bindingMask == 0U || track.keyframes.empty() || !paths.insert(track.targetPath).second) return false;
        float previous = -1.0F;
        for (AnimationTransformKeyframe& key : track.keyframes) {
            const LocalTransform& transform = key.transform;
            if (!std::isfinite(key.timeSeconds) || key.timeSeconds < 0.0F || key.timeSeconds > clip.durationSeconds ||
                key.timeSeconds < previous ||
                !std::isfinite(transform.position.x) || !std::isfinite(transform.position.y) || !std::isfinite(transform.position.z) ||
                !std::isfinite(transform.rotation.x) || !std::isfinite(transform.rotation.y) ||
                !std::isfinite(transform.rotation.z) || !std::isfinite(transform.rotation.w) ||
                !std::isfinite(transform.scale.x) || !std::isfinite(transform.scale.y) || !std::isfinite(transform.scale.z)) return false;
            key.transform.rotation = kb::math::Normalize(key.transform.rotation);
            previous = key.timeSeconds;
        }
    }
    float previousEventTime = -1.0F;
    AnimationEventId previousEventId = 0U;
    setError("Animation clip has invalid or unordered events.");
    for (const AnimationEventKeyframe& event : clip.events) {
        const bool sameTime = event.timeSeconds == previousEventTime;
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds <= 0.0F ||
            event.timeSeconds > clip.durationSeconds ||
            (clip.looping && event.timeSeconds >= clip.durationSeconds) ||
            event.id == 0U || event.timeSeconds < previousEventTime ||
            (sameTime && event.id <= previousEventId)) return false;
        previousEventTime = event.timeSeconds;
        previousEventId = event.id;
    }
    const bool skeletal =
        clip.targetSkeletonAssetId != 0U ||
        clip.targetSkeletonCompatibilitySignature != 0U ||
        !clip.skeletalTracks.empty() || !clip.morphTracks.empty() ||
        !clip.curves.empty() ||
        clip.rootMotionMode != AnimationRootMotionMode::None;
    if (!skeletal) {
        if (!clip.tracks.empty()) return true;
        setError("Generic animation clip has no transform tracks.");
        return false;
    }
    setError("Skeletal animation clip has an invalid skeleton target or mixed bindings.");
    if (clip.targetSkeletonAssetId == 0U ||
        clip.targetSkeletonCompatibilitySignature == 0U ||
        !clip.tracks.empty() ||
        (clip.skeletalTracks.empty() && clip.morphTracks.empty() && clip.curves.empty())) {
        return false;
    }
    std::unordered_set<SkeletonBoneId> bones;
    setError("Skeletal animation clip has invalid stable bone bindings or keyframes.");
    for (AnimationBoneTrack& track : clip.skeletalTracks) {
        if (track.boneId == 0U || track.bindingMask == 0U ||
            !bones.insert(track.boneId).second ||
            track.keyframes.empty()) {
            return false;
        }
        float previous = -1.0F;
        for (AnimationBoneKeyframe& key : track.keyframes) {
            const LocalTransform& transform = key.transform;
            if (!std::isfinite(key.timeSeconds) || key.timeSeconds < 0.0F ||
                key.timeSeconds > clip.durationSeconds || key.timeSeconds < previous ||
                !std::isfinite(transform.position.x) || !std::isfinite(transform.position.y) ||
                !std::isfinite(transform.position.z) || !std::isfinite(transform.rotation.x) ||
                !std::isfinite(transform.rotation.y) || !std::isfinite(transform.rotation.z) ||
                !std::isfinite(transform.rotation.w) || !std::isfinite(transform.scale.x) ||
                !std::isfinite(transform.scale.y) || !std::isfinite(transform.scale.z)) {
                return false;
            }
            key.transform.rotation = kb::math::Normalize(key.transform.rotation);
            previous = key.timeSeconds;
        }
    }
    std::unordered_set<std::string> morphs;
    setError("Skeletal animation clip has invalid morph channels or keyframes.");
    for (const AnimationMorphTrack& track : clip.morphTracks) {
        if (track.morphTarget.empty() || !morphs.insert(track.morphTarget).second ||
            track.keyframes.empty()) {
            return false;
        }
        float previous = -1.0F;
        for (const AnimationMorphKeyframe& key : track.keyframes) {
            if (!std::isfinite(key.timeSeconds) || !std::isfinite(key.weight) ||
                key.timeSeconds < 0.0F || key.timeSeconds > clip.durationSeconds ||
                key.timeSeconds < previous) {
                return false;
            }
            previous = key.timeSeconds;
        }
    }
    std::unordered_set<std::string> curves;
    setError("Skeletal animation clip has invalid curve channels or keyframes.");
    for (const AnimationCurveTrack& track : clip.curves) {
        if (track.name.empty() || !curves.insert(track.name).second || track.keyframes.empty()) return false;
        float previous = -1.0F;
        for (const AnimationCurveKeyframe& key : track.keyframes) {
            if (!std::isfinite(key.timeSeconds) || !std::isfinite(key.value) ||
                key.timeSeconds < 0.0F || key.timeSeconds > clip.durationSeconds ||
                key.timeSeconds < previous) {
                return false;
            }
            previous = key.timeSeconds;
        }
    }
    const bool validRootMotion = clip.rootMotionMode == AnimationRootMotionMode::None
        ? clip.rootMotionBoneId == 0U
        : clip.rootMotionBoneId != 0U && bones.contains(clip.rootMotionBoneId);
    if (!validRootMotion) {
        setError("Skeletal animation clip has an invalid root-motion bone binding.");
    }
    return validRootMotion;
}

bool ValidateController(const AnimatorController& controller, std::string* error = nullptr) {
    const auto setError = [error](const char* message) {
        if (error != nullptr) *error = message;
    };
    setError("Animator controller has no layers.");
    if (controller.layers.empty()) return false;
    setError("Animator controller has invalid parameter definitions.");
    std::unordered_set<std::string> parameterNames;
    for (const AnimatorParameterDefinition& parameter : controller.parameters) {
        if (parameter.name.empty() || !parameterNames.insert(parameter.name).second ||
            !std::isfinite(parameter.floatDefault) ||
            (parameter.type == AnimatorParameterType::Trigger && parameter.boolDefault)) return false;
    }
    std::unordered_set<std::string> layerNames;
    setError("Animator controller has invalid layer or state definitions.");
    for (const AnimatorControllerLayer& layer : controller.layers) {
        if (layer.name.empty() || !layerNames.insert(layer.name).second || layer.defaultState.empty() ||
            !std::isfinite(layer.weight) || layer.weight < 0.0F || layer.weight > 1.0F || layer.mask == 0U ||
            layer.states.empty()) return false;
        std::unordered_set<std::string> stateNames;
        bool foundDefault = false;
        for (const AnimatorControllerState& state : layer.states) {
            if (state.name.empty() || !stateNames.insert(state.name).second) return false;
            const bool hasClip = !state.clipReference.empty();
            const bool hasBlendTree = !state.blendParameter.empty() || !state.blendChildren.empty();
            if (hasClip == hasBlendTree) return false;
            if (hasBlendTree) {
                setError("Animator controller has an invalid blend tree.");
                const auto parameter = std::find_if(controller.parameters.begin(), controller.parameters.end(),
                    [&](const AnimatorParameterDefinition& value) {
                        return value.name == state.blendParameter &&
                            value.type == AnimatorParameterType::Float;
                    });
                if (parameter == controller.parameters.end() || state.blendChildren.size() < 2U) return false;
                float previousThreshold = -std::numeric_limits<float>::infinity();
                for (const AnimatorControllerState::BlendChild& child : state.blendChildren) {
                    if (!std::isfinite(child.threshold) || child.threshold <= previousThreshold ||
                        child.clipReference.empty()) return false;
                    previousThreshold = child.threshold;
                }
            }
            foundDefault |= state.name == layer.defaultState;
        }
        setError("Animator controller layer has no declared default state.");
        if (!foundDefault) return false;
        setError("Animator controller has an invalid transition or condition.");
        for (const AnimatorControllerTransition& transition : layer.transitions) {
            if (!stateNames.contains(transition.fromState) || !stateNames.contains(transition.toState) ||
                transition.fromState == transition.toState || !std::isfinite(transition.durationSeconds) ||
                transition.durationSeconds < 0.0F || !std::isfinite(transition.exitNormalizedTime) ||
                transition.exitNormalizedTime > 1.0F || transition.conditions.empty()) return false;
            for (const AnimatorTransitionCondition& condition : transition.conditions) {
                const auto parameter = std::find_if(controller.parameters.begin(), controller.parameters.end(),
                    [&](const AnimatorParameterDefinition& value) { return value.name == condition.parameter; });
                if (parameter == controller.parameters.end() || parameter->type != ConditionParameterType(condition.mode) ||
                    !std::isfinite(condition.floatValue)) return false;
            }
        }
    }
    std::unordered_set<std::string> constraintNames;
    std::unordered_set<std::string> drivenPaths;
    std::unordered_set<SkeletonBoneId> drivenBones;
    setError("Animator controller has invalid rig constraints.");
    for (const AnimatorRigConstraint& constraint : controller.rigConstraints) {
        if (constraint.name.empty() || !constraintNames.insert(constraint.name).second ||
            constraint.target.empty() || !std::isfinite(constraint.weight) ||
            constraint.weight <= 0.0F || constraint.weight > 1.0F) return false;
        const bool skeletal = constraint.constrainedBoneId != 0U ||
            constraint.midBoneId != 0U || constraint.tipBoneId != 0U;
        if (skeletal && (!constraint.constrainedPath.empty() ||
                         !constraint.midPath.empty() ||
                         !constraint.tipPath.empty())) return false;
        switch (constraint.type) {
        case AnimatorRigConstraintType::TwoBoneIK:
            if (skeletal) {
                if (constraint.constrainedBoneId == 0U ||
                    constraint.midBoneId == 0U ||
                    constraint.tipBoneId == 0U ||
                    constraint.constrainedBoneId == constraint.midBoneId ||
                    constraint.constrainedBoneId == constraint.tipBoneId ||
                    constraint.midBoneId == constraint.tipBoneId ||
                    !drivenBones.insert(constraint.constrainedBoneId).second ||
                    !drivenBones.insert(constraint.midBoneId).second ||
                    !drivenBones.insert(constraint.tipBoneId).second) return false;
            } else if (constraint.midPath.empty() || constraint.tipPath.empty() ||
                       constraint.constrainedPath == constraint.midPath ||
                       constraint.constrainedPath == constraint.tipPath ||
                       constraint.midPath == constraint.tipPath ||
                       !drivenPaths.insert(
                           constraint.constrainedPath.empty() ? "." :
                           constraint.constrainedPath).second ||
                       !drivenPaths.insert(constraint.midPath).second ||
                       !drivenPaths.insert(constraint.tipPath).second) return false;
            break;
        case AnimatorRigConstraintType::Aim:
        case AnimatorRigConstraintType::CopyTransform:
            if (skeletal) {
                if (constraint.constrainedBoneId == 0U ||
                    constraint.midBoneId != 0U || constraint.tipBoneId != 0U ||
                    !drivenBones.insert(constraint.constrainedBoneId).second ||
                    !constraint.poleTarget.empty()) return false;
            } else if (!drivenPaths.insert(
                           constraint.constrainedPath.empty() ? "." :
                           constraint.constrainedPath).second ||
                       !constraint.midPath.empty() ||
                       !constraint.tipPath.empty() ||
                       !constraint.poleTarget.empty()) return false;
            break;
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

} // namespace

std::optional<AnimationClip> AnimationAssetIO::LoadClip(
    const std::filesystem::path& path,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<AnimationClip> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (path.extension() != kAnimationClipAssetExtension) {
        return fail("Animation clip has an unexpected file extension.");
    }
    const auto text = ReadText(path);
    if (!text) return fail("Animation clip could not be read.");
    AnimationClip clip{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    bool schemaRead = false;
    std::uint32_t schemaVersion = 0U;
    std::size_t lineNumber = 0U;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (error != nullptr) {
            *error = "Animation clip has an invalid record at line " +
                std::to_string(lineNumber) + ".";
        }
        if (!schemaRead) {
            if (line.starts_with("21kb")) {
                std::istringstream headerInput{ line };
                headerInput.imbue(std::locale::classic());
                std::string magic;
                std::string type;
                if (!(headerInput >> magic >> type >> schemaVersion) ||
                    magic != "21kb" || type != kAnimationClipAssetType ||
                    schemaVersion == 0U ||
                    schemaVersion > kAnimationClipAssetSchemaVersion ||
                    !EndOfRecord(headerInput)) {
                    return std::nullopt;
                }
                schemaRead = true;
                continue;
            }
            schemaRead = true;
        }
        if (command == "durationSeconds") {
            if (!(input >> clip.durationSeconds) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "looping") {
            std::string value;
            if (!(input >> value) || !ParseBool(value, clip.looping) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "track") {
            AnimationTransformTrack track{};
            if (!(input >> std::quoted(track.targetPath) >> track.bindingMask) || !EndOfRecord(input)) return std::nullopt;
            if (track.targetPath == ".") track.targetPath.clear();
            clip.tracks.push_back(std::move(track));
        } else if (command == "key") {
            std::size_t trackIndex = 0U;
            AnimationTransformKeyframe key{};
            LocalTransform& value = key.transform;
            if (!(input >> trackIndex >> key.timeSeconds >>
                    value.position.x >> value.position.y >> value.position.z >>
                    value.rotation.x >> value.rotation.y >> value.rotation.z >> value.rotation.w >>
                    value.scale.x >> value.scale.y >> value.scale.z) ||
                trackIndex >= clip.tracks.size() || !EndOfRecord(input)) return std::nullopt;
            clip.tracks[trackIndex].keyframes.push_back(key);
        } else if (command == "event") {
            AnimationEventKeyframe event{};
            if (!(input >> event.timeSeconds >> event.id) || !EndOfRecord(input)) return std::nullopt;
            clip.events.push_back(event);
        } else if (command == "targetSkeleton") {
            if (!(input >> clip.targetSkeletonAssetId >> clip.targetSkeletonCompatibilitySignature) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "skeletalTrack") {
            AnimationBoneTrack track{};
            if (!(input >> track.boneId)) return std::nullopt;
            if (schemaVersion >= 2U) {
                if (!(input >> track.bindingMask) || !EndOfRecord(input)) {
                    return std::nullopt;
                }
            } else if (!EndOfRecord(input)) {
                return std::nullopt;
            }
            clip.skeletalTracks.push_back(std::move(track));
        } else if (command == "skeletalKey") {
            std::size_t trackIndex = 0U;
            AnimationBoneKeyframe key{};
            LocalTransform& value = key.transform;
            if (!(input >> trackIndex >> key.timeSeconds >>
                    value.position.x >> value.position.y >> value.position.z >>
                    value.rotation.x >> value.rotation.y >> value.rotation.z >> value.rotation.w >>
                    value.scale.x >> value.scale.y >> value.scale.z) ||
                trackIndex >= clip.skeletalTracks.size() || !EndOfRecord(input)) {
                return std::nullopt;
            }
            clip.skeletalTracks[trackIndex].keyframes.push_back(key);
        } else if (command == "morphTrack") {
            AnimationMorphTrack track{};
            if (!(input >> std::quoted(track.morphTarget)) || !EndOfRecord(input)) return std::nullopt;
            clip.morphTracks.push_back(std::move(track));
        } else if (command == "morphKey") {
            std::size_t trackIndex = 0U;
            AnimationMorphKeyframe key{};
            if (!(input >> trackIndex >> key.timeSeconds >> key.weight) ||
                trackIndex >= clip.morphTracks.size() || !EndOfRecord(input)) {
                return std::nullopt;
            }
            clip.morphTracks[trackIndex].keyframes.push_back(key);
        } else if (command == "curve") {
            AnimationCurveTrack track{};
            if (!(input >> std::quoted(track.name)) || !EndOfRecord(input)) return std::nullopt;
            clip.curves.push_back(std::move(track));
        } else if (command == "curveKey") {
            std::size_t trackIndex = 0U;
            AnimationCurveKeyframe key{};
            if (!(input >> trackIndex >> key.timeSeconds >> key.value) ||
                trackIndex >= clip.curves.size() || !EndOfRecord(input)) {
                return std::nullopt;
            }
            clip.curves[trackIndex].keyframes.push_back(key);
        } else if (command == "rootMotion") {
            std::string mode;
            if (!(input >> mode >> clip.rootMotionBoneId) || !EndOfRecord(input)) {
                return std::nullopt;
            }
            if (mode == "None") {
                clip.rootMotionMode = AnimationRootMotionMode::None;
            } else if (mode == "ExtractFromBone") {
                clip.rootMotionMode = AnimationRootMotionMode::ExtractFromBone;
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (!ValidateClip(clip, error)) return std::nullopt;
    if (error != nullptr) error->clear();
    return clip;
}

std::optional<AnimatorController> AnimationAssetIO::LoadController(
    const std::filesystem::path& path,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<AnimatorController> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (path.extension() != kAnimatorControllerAssetExtension) {
        return fail("Animator controller has an unexpected file extension.");
    }
    const auto text = ReadText(path);
    if (!text) return fail("Animator controller could not be read.");
    AnimatorController controller{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    bool schemaRead = false;
    std::uint32_t schemaVersion = 0U;
    std::size_t lineNumber = 0U;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (error != nullptr) {
            *error = "Animator controller has an invalid record at line " +
                std::to_string(lineNumber) + ".";
        }
        if (!schemaRead) {
            if (line.starts_with("21kb")) {
                std::istringstream headerInput{ line };
                headerInput.imbue(std::locale::classic());
                std::string magic;
                std::string type;
                if (!(headerInput >> magic >> type >> schemaVersion) ||
                    magic != "21kb" ||
                    type != kAnimatorControllerAssetType ||
                    schemaVersion == 0U ||
                    schemaVersion > kAnimatorControllerAssetSchemaVersion ||
                    !EndOfRecord(headerInput)) return std::nullopt;
                schemaRead = true;
                continue;
            }
            schemaRead = true;
        }
        if (command == "parameter") {
            AnimatorParameterDefinition parameter{};
            std::string type;
            std::string value;
            if (!(input >> std::quoted(parameter.name) >> type >> value) || !EndOfRecord(input) ||
                !ParseParameterType(type, parameter.type)) return std::nullopt;
            std::istringstream parsed{ value };
            parsed.imbue(std::locale::classic());
            switch (parameter.type) {
            case AnimatorParameterType::Bool:
                if (!ParseBool(value, parameter.boolDefault)) return std::nullopt;
                break;
            case AnimatorParameterType::Trigger:
                if (!ParseBool(value, parameter.boolDefault) || parameter.boolDefault) return std::nullopt;
                break;
            case AnimatorParameterType::Int:
                if (!(parsed >> parameter.intDefault) || !parsed.eof()) return std::nullopt;
                break;
            case AnimatorParameterType::Float:
                if (!(parsed >> parameter.floatDefault) || !parsed.eof()) return std::nullopt;
                break;
            }
            controller.parameters.push_back(std::move(parameter));
        } else if (command == "layer") {
            AnimatorControllerLayer layer{};
            if (!(input >> std::quoted(layer.name) >> std::quoted(layer.defaultState) >> layer.weight >> layer.mask) ||
                !EndOfRecord(input)) return std::nullopt;
            controller.layers.push_back(std::move(layer));
        } else if (command == "state") {
            std::size_t layerIndex = 0U;
            AnimatorControllerState state{};
            if (!(input >> layerIndex >> std::quoted(state.name) >> std::quoted(state.clipReference)) ||
                layerIndex >= controller.layers.size() || !EndOfRecord(input)) return std::nullopt;
            controller.layers[layerIndex].states.push_back(std::move(state));
        } else if (command == "blend1D") {
            std::size_t layerIndex = 0U;
            std::size_t stateIndex = 0U;
            std::string parameter;
            if (!(input >> layerIndex >> stateIndex >> std::quoted(parameter)) ||
                layerIndex >= controller.layers.size() ||
                stateIndex >= controller.layers[layerIndex].states.size() ||
                !EndOfRecord(input)) return std::nullopt;
            controller.layers[layerIndex].states[stateIndex].blendParameter = std::move(parameter);
        } else if (command == "blendChild") {
            std::size_t layerIndex = 0U;
            std::size_t stateIndex = 0U;
            AnimatorControllerState::BlendChild child{};
            if (!(input >> layerIndex >> stateIndex >> child.threshold >>
                    std::quoted(child.clipReference)) ||
                layerIndex >= controller.layers.size() ||
                stateIndex >= controller.layers[layerIndex].states.size() ||
                !EndOfRecord(input)) return std::nullopt;
            controller.layers[layerIndex].states[stateIndex].blendChildren.push_back(
                std::move(child));
        } else if (command == "transition") {
            std::size_t layerIndex = 0U;
            AnimatorControllerTransition transition{};
            if (!(input >> layerIndex >> std::quoted(transition.fromState) >> std::quoted(transition.toState) >>
                    transition.durationSeconds >> transition.exitNormalizedTime) ||
                layerIndex >= controller.layers.size() || !EndOfRecord(input)) return std::nullopt;
            controller.layers[layerIndex].transitions.push_back(std::move(transition));
        } else if (command == "condition") {
            std::size_t layerIndex = 0U;
            std::size_t transitionIndex = 0U;
            AnimatorTransitionCondition condition{};
            std::string mode;
            std::string value;
            if (!(input >> layerIndex >> transitionIndex >> std::quoted(condition.parameter) >> mode >> value) ||
                layerIndex >= controller.layers.size() ||
                transitionIndex >= controller.layers[layerIndex].transitions.size() ||
                !ParseConditionMode(mode, condition.mode) || !EndOfRecord(input)) return std::nullopt;
            std::istringstream parsed{ value };
            parsed.imbue(std::locale::classic());
            switch (condition.mode) {
            case AnimatorConditionMode::BoolEquals:
                if (!ParseBool(value, condition.boolValue)) return std::nullopt;
                break;
            case AnimatorConditionMode::TriggerSet:
                if (value != "1" && value != "true") return std::nullopt;
                break;
            case AnimatorConditionMode::IntEquals:
            case AnimatorConditionMode::IntGreater:
            case AnimatorConditionMode::IntLess:
                if (!(parsed >> condition.intValue) || !parsed.eof()) return std::nullopt;
                break;
            case AnimatorConditionMode::FloatGreater:
            case AnimatorConditionMode::FloatLess:
                if (!(parsed >> condition.floatValue) || !parsed.eof()) return std::nullopt;
                break;
            }
            controller.layers[layerIndex].transitions[transitionIndex].conditions.push_back(std::move(condition));
        } else if (command == "constraint") {
            AnimatorRigConstraint constraint{};
            std::string type;
            if (!(input >> type >> std::quoted(constraint.name) >>
                    std::quoted(constraint.constrainedPath) >>
                    std::quoted(constraint.midPath) >>
                    std::quoted(constraint.tipPath) >>
                    std::quoted(constraint.target) >>
                    std::quoted(constraint.poleTarget) >> constraint.weight) ||
                !ParseRigConstraintType(type, constraint.type) ||
                !EndOfRecord(input)) return std::nullopt;
            if (constraint.constrainedPath == ".") constraint.constrainedPath.clear();
            controller.rigConstraints.push_back(std::move(constraint));
        } else if (command == "skeletalConstraint") {
            if (schemaVersion < 2U) return std::nullopt;
            AnimatorRigConstraint constraint{};
            std::string type;
            if (!(input >> type >> std::quoted(constraint.name) >>
                    constraint.constrainedBoneId >> constraint.midBoneId >>
                    constraint.tipBoneId >> std::quoted(constraint.target) >>
                    std::quoted(constraint.poleTarget) >> constraint.weight) ||
                !ParseRigConstraintType(type, constraint.type) ||
                !EndOfRecord(input)) return std::nullopt;
            controller.rigConstraints.push_back(std::move(constraint));
        } else {
            return std::nullopt;
        }
    }
    if (!ValidateController(controller, error)) return std::nullopt;
    if (error != nullptr) error->clear();
    return controller;
}

bool AnimationAssetIO::SaveClip(const std::filesystem::path& path, const AnimationClip& source) {
    AnimationClip clip = source;
    if (!ValidateClip(clip)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << asset_io::TextAssetHeader(
        kAnimationClipAssetType, kAnimationClipAssetSchemaVersion);
    output << "durationSeconds " << clip.durationSeconds << "\nlooping " << (clip.looping ? 1 : 0) << '\n';
    for (const auto& track : clip.tracks) output << "track " << std::quoted(track.targetPath.empty() ? "." : track.targetPath) << ' ' << track.bindingMask << '\n';
    for (std::size_t index = 0U; index < clip.tracks.size(); ++index) {
        for (const auto& key : clip.tracks[index].keyframes) {
            const auto& value = key.transform;
            output << "key " << index << ' ' << key.timeSeconds << ' ' << value.position.x << ' ' << value.position.y << ' ' << value.position.z
                   << ' ' << value.rotation.x << ' ' << value.rotation.y << ' ' << value.rotation.z << ' ' << value.rotation.w
                   << ' ' << value.scale.x << ' ' << value.scale.y << ' ' << value.scale.z << '\n';
        }
    }
    for (const AnimationEventKeyframe& event : clip.events) {
        output << "event " << event.timeSeconds << ' ' << event.id << '\n';
    }
    if (clip.targetSkeletonAssetId != 0U) {
        output << "targetSkeleton " << clip.targetSkeletonAssetId << ' '
               << clip.targetSkeletonCompatibilitySignature << '\n';
        for (std::size_t index = 0U; index < clip.skeletalTracks.size(); ++index) {
            const AnimationBoneTrack& track = clip.skeletalTracks[index];
            output << "skeletalTrack " << track.boneId << ' '
                   << track.bindingMask << '\n';
            for (const AnimationBoneKeyframe& key : track.keyframes) {
                const LocalTransform& value = key.transform;
                output << "skeletalKey " << index << ' ' << key.timeSeconds << ' '
                       << value.position.x << ' ' << value.position.y << ' ' << value.position.z << ' '
                       << value.rotation.x << ' ' << value.rotation.y << ' ' << value.rotation.z << ' ' << value.rotation.w << ' '
                       << value.scale.x << ' ' << value.scale.y << ' ' << value.scale.z << '\n';
            }
        }
        for (std::size_t index = 0U; index < clip.morphTracks.size(); ++index) {
            const AnimationMorphTrack& track = clip.morphTracks[index];
            output << "morphTrack " << std::quoted(track.morphTarget) << '\n';
            for (const AnimationMorphKeyframe& key : track.keyframes) {
                output << "morphKey " << index << ' ' << key.timeSeconds << ' ' << key.weight << '\n';
            }
        }
        for (std::size_t index = 0U; index < clip.curves.size(); ++index) {
            const auto& curve = clip.curves[index];
            output << "curve " << std::quoted(curve.name) << '\n';
            for (const auto& key : curve.keyframes) {
                output << "curveKey " << index << ' ' << key.timeSeconds << ' ' << key.value << '\n';
            }
        }
        output << "rootMotion "
               << (clip.rootMotionMode == AnimationRootMotionMode::ExtractFromBone ? "ExtractFromBone" : "None")
               << ' ' << clip.rootMotionBoneId << '\n';
    }
    return WriteText(path, output.str());
}

bool AnimationAssetIO::SaveController(const std::filesystem::path& path, const AnimatorController& controller) {
    if (!ValidateController(controller)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << asset_io::TextAssetHeader(
        kAnimatorControllerAssetType, kAnimatorControllerAssetSchemaVersion);
    for (const auto& parameter : controller.parameters) {
        output << "parameter " << std::quoted(parameter.name) << ' ' << ParameterTypeName(parameter.type) << ' ';
        if (parameter.type == AnimatorParameterType::Int) output << parameter.intDefault;
        else if (parameter.type == AnimatorParameterType::Float) output << parameter.floatDefault;
        else output << (parameter.boolDefault ? 1 : 0);
        output << '\n';
    }
    for (const auto& layer : controller.layers) {
        output << "layer " << std::quoted(layer.name) << ' ' << std::quoted(layer.defaultState) << ' ' << layer.weight << ' ' << layer.mask << '\n';
        const std::size_t layerIndex = static_cast<std::size_t>(&layer - controller.layers.data());
        for (const auto& state : layer.states) {
            output << "state " << layerIndex << ' ' << std::quoted(state.name) << ' ' << std::quoted(state.clipReference) << '\n';
            const std::size_t stateIndex =
                static_cast<std::size_t>(&state - layer.states.data());
            if (!state.blendChildren.empty()) {
                output << "blend1D " << layerIndex << ' ' << stateIndex << ' '
                       << std::quoted(state.blendParameter) << '\n';
                for (const AnimatorControllerState::BlendChild& child : state.blendChildren) {
                    output << "blendChild " << layerIndex << ' ' << stateIndex << ' '
                           << child.threshold << ' ' << std::quoted(child.clipReference) << '\n';
                }
            }
        }
        for (std::size_t transitionIndex = 0U; transitionIndex < layer.transitions.size(); ++transitionIndex) {
            const AnimatorControllerTransition& transition = layer.transitions[transitionIndex];
            output << "transition " << layerIndex << ' ' << std::quoted(transition.fromState) << ' '
                   << std::quoted(transition.toState) << ' ' << transition.durationSeconds << ' '
                   << transition.exitNormalizedTime << '\n';
            for (const AnimatorTransitionCondition& condition : transition.conditions) {
                output << "condition " << layerIndex << ' ' << transitionIndex << ' '
                       << std::quoted(condition.parameter) << ' ' << ConditionModeName(condition.mode) << ' ';
                switch (condition.mode) {
                case AnimatorConditionMode::BoolEquals: output << (condition.boolValue ? 1 : 0); break;
                case AnimatorConditionMode::TriggerSet: output << 1; break;
                case AnimatorConditionMode::IntEquals:
                case AnimatorConditionMode::IntGreater:
                case AnimatorConditionMode::IntLess: output << condition.intValue; break;
                case AnimatorConditionMode::FloatGreater:
                case AnimatorConditionMode::FloatLess: output << condition.floatValue; break;
                }
                output << '\n';
            }
        }
    }
    for (const AnimatorRigConstraint& constraint : controller.rigConstraints) {
        if (constraint.constrainedBoneId != 0U) {
            output << "skeletalConstraint "
                   << RigConstraintTypeName(constraint.type) << ' '
                   << std::quoted(constraint.name) << ' '
                   << constraint.constrainedBoneId << ' '
                   << constraint.midBoneId << ' '
                   << constraint.tipBoneId << ' '
                   << std::quoted(constraint.target) << ' '
                   << std::quoted(constraint.poleTarget) << ' '
                   << constraint.weight << '\n';
            continue;
        }
        output << "constraint " << RigConstraintTypeName(constraint.type) << ' '
               << std::quoted(constraint.name) << ' '
               << std::quoted(
                      constraint.constrainedPath.empty() ? "." : constraint.constrainedPath)
               << ' ' << std::quoted(constraint.midPath) << ' '
               << std::quoted(constraint.tipPath) << ' '
               << std::quoted(constraint.target) << ' '
               << std::quoted(constraint.poleTarget) << ' '
               << constraint.weight << '\n';
    }
    return WriteText(path, output.str());
}

} // namespace kb::scene
