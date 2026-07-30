#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetCameraParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetInputParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetLightParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetMeshRendererParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetTagsParser.hpp"

#include <cmath>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

template <typename T>
[[nodiscard]] bool ParseOptionalField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator == fields.end() || ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

[[nodiscard]] bool ParseOptionalComponentFlag(const ScenePrefabAssetFieldMap& fields, std::string_view key, bool& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        output = false;
        return true;
    }

    int value = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(iterator->second, value) || (value != 0 && value != 1)) {
        return false;
    }
    output = value != 0;
    return true;
}

[[nodiscard]] bool ParseOptionalBool(const ScenePrefabAssetFieldMap& fields, std::string_view key, bool& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return true;
    }

    int value = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(iterator->second, value) || (value != 0 && value != 1)) {
        return false;
    }
    output = value != 0;
    return true;
}

[[nodiscard]] bool ParseRigidbody(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasRigidbody = false;
    if (!ParseOptionalComponentFlag(fields, "rigidbody", hasRigidbody)) {
        return false;
    }
    if (!hasRigidbody) {
        return true;
    }

    int bodyType = 0;
    RigidbodyComponent rigidbody;
    if (!ParseField(fields, "rigidbody.bodyType", bodyType)
        || bodyType < static_cast<int>(RigidbodyBodyType::Static)
        || bodyType > static_cast<int>(RigidbodyBodyType::Kinematic)
        || !ParseField(fields, "rigidbody.mass", rigidbody.mass)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "rigidbody.linearVelocity", rigidbody.linearVelocity)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "rigidbody.angularVelocity", rigidbody.angularVelocity)
        || !ParseField(fields, "rigidbody.gravityScale", rigidbody.gravityScale)
        || !ParseOptionalBool(fields, "rigidbody.useGravity", rigidbody.useGravity)
        || !ParseOptionalBool(fields, "rigidbody.lockRotation", rigidbody.lockRotation)) {
        return false;
    }

    rigidbody.bodyType = static_cast<RigidbodyBodyType>(bodyType);
    components.rigidbody = rigidbody;
    return true;
}

[[nodiscard]] bool ParseCollider(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasCollider = false;
    if (!ParseOptionalComponentFlag(fields, "collider", hasCollider)) {
        return false;
    }
    if (!hasCollider) {
        return true;
    }

    int shape = 0;
    ColliderComponent collider;
    if (!ParseField(fields, "collider.shape", shape)
        || shape < static_cast<int>(ColliderShape::Box)
        || shape > static_cast<int>(ColliderShape::Capsule)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "collider.center", collider.center)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "collider.boxSize", collider.boxSize)
        || !ParseField(fields, "collider.radius", collider.radius)
        || !ParseField(fields, "collider.height", collider.height)
        || !ParseOptionalBool(fields, "collider.trigger", collider.trigger)
        // These fields were added after the first textual prefab format.
        // Missing values intentionally retain ColliderComponent's production
        // defaults so existing assets remain loadable; newly written assets
        // always persist them below.
        || !ParseOptionalField(fields, "collider.friction", collider.friction)
        || !ParseOptionalField(fields, "collider.restitution", collider.restitution)
        || !ParseOptionalField(fields, "collider.layer", collider.layer)) {
        return false;
    }

    collider.shape = static_cast<ColliderShape>(shape);
    components.collider = collider;
    return true;
}

[[nodiscard]] bool ParseRegionShape(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasRegionShape = false;
    if (!ParseOptionalComponentFlag(fields, "regionShape", hasRegionShape)) return false;
    if (!hasRegionShape) return true;
    int kind = 0;
    RegionShapeComponent regionShape{};
    if (!ParseField(fields, "regionShape.kind", kind)
        || kind < static_cast<int>(RegionShapeKind::Circle2D)
        || kind > static_cast<int>(RegionShapeKind::Capsule)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "regionShape.center", regionShape.center)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "regionShape.size", regionShape.size)
        || !ParseField(fields, "regionShape.radius", regionShape.radius)
        || !ParseField(fields, "regionShape.height", regionShape.height)
        || !ParseOptionalBool(fields, "regionShape.enabled", regionShape.enabled)) return false;
    regionShape.kind = static_cast<RegionShapeKind>(kind);
    components.regionShape = regionShape;
    return true;
}

[[nodiscard]] bool ParseGuideCurve(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasGuideCurve = false;
    if (!ParseOptionalComponentFlag(fields, "guideCurve", hasGuideCurve)) return false;
    if (!hasGuideCurve) return true;
    GuideCurveComponent guideCurve{};
    int interpolation = 0;
    if (!ParseField(fields, "guideCurve.controlPointCount", guideCurve.controlPointCount)
        || !IsGuideCurveControlPointCountValid(guideCurve.controlPointCount)
        || !ParseField(fields, "guideCurve.interpolation", interpolation)
        || interpolation < static_cast<int>(GuideCurveInterpolation::Linear)
        || interpolation > static_cast<int>(GuideCurveInterpolation::CatmullRom)
        || !ParseOptionalBool(fields, "guideCurve.closed", guideCurve.closed)
        || !ParseOptionalBool(fields, "guideCurve.enabled", guideCurve.enabled)) return false;
    for (std::uint32_t index = 0U; index < guideCurve.controlPointCount; ++index) {
        if (!ScenePrefabAssetFieldParser::ParseVec3(fields, "guideCurve.point" + std::to_string(index), guideCurve.controlPoints[index])) return false;
    }
    guideCurve.interpolation = static_cast<GuideCurveInterpolation>(interpolation);
    components.guideCurve = guideCurve;
    return true;
}

[[nodiscard]] bool ParseContentInstance(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasContent = false;
    if (!ParseOptionalComponentFlag(fields, "contentInstance", hasContent)) return false;
    if (!hasContent) return true;
    ContentInstanceComponent content{};
    int kind = 0;
    int lifetime = 0;
    if (!ParseField(fields, "contentInstance.assetId", content.assetId) ||
        !ParseField(fields, "contentInstance.kind", kind) ||
        !ParseField(fields, "contentInstance.lifetime", lifetime) ||
        !ParseOptionalBool(fields, "contentInstance.active", content.active) ||
        kind < static_cast<int>(ContentInstanceKind::Prefab) || kind > static_cast<int>(ContentInstanceKind::WorldFragment) ||
        lifetime < static_cast<int>(ContentInstanceLifetime::Owner) || lifetime > static_cast<int>(ContentInstanceLifetime::Persistent)) return false;
    content.kind = static_cast<ContentInstanceKind>(kind);
    content.lifetime = static_cast<ContentInstanceLifetime>(lifetime);
    components.contentInstance = content;
    return true;
}

[[nodiscard]] bool ParseStreamFocus(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool present = false;
    if (!ParseOptionalComponentFlag(fields, "streamFocus", present)) return false;
    if (!present) return true;
    StreamFocusComponent focus{};
    std::uint32_t mask = 0U;
    if (!ParseField(fields, "streamFocus.innerRadius", focus.innerRadius) || !ParseField(fields, "streamFocus.outerRadius", focus.outerRadius) || !ParseField(fields, "streamFocus.priority", focus.priority) || !ParseField(fields, "streamFocus.loadMask", mask) || !ParseOptionalBool(fields, "streamFocus.enabled", focus.enabled)) return false;
    focus.loadMask = static_cast<StreamLoadMask>(mask);
    if (!IsStreamFocusValid(focus)) return false;
    components.streamFocus = focus;
    return true;
}

[[nodiscard]] bool ParseCharacterController(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasCharacterController = false;
    if (!ParseOptionalComponentFlag(fields, "characterController", hasCharacterController)) {
        return false;
    }
    if (!hasCharacterController) {
        return true;
    }

    CharacterControllerComponent characterController;
    if (!ScenePrefabAssetFieldParser::ParseVec3(fields, "characterController.center", characterController.center)
        || !ParseField(fields, "characterController.radius", characterController.radius)
        || !ParseField(fields, "characterController.height", characterController.height)
        || !ParseOptionalField(fields, "characterController.slopeLimitDegrees", characterController.slopeLimitDegrees)
        || !ParseOptionalField(fields, "characterController.stepOffset", characterController.stepOffset)
        || !ParseOptionalField(fields, "characterController.gravityScale", characterController.gravityScale)
        || !ParseOptionalBool(fields, "characterController.useGravity", characterController.useGravity)) {
        return false;
    }

    components.characterController = characterController;
    return true;
}

[[nodiscard]] bool ParseJoint(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasJoint = false;
    if (!ParseOptionalComponentFlag(fields, "joint", hasJoint)) {
        return false;
    }
    if (!hasJoint) {
        return true;
    }

    int type = 0;
    ScenePrefabJointComponent joint;
    if (!ParseField(fields, "joint.type", type)
        || type < static_cast<int>(JointType::Fixed)
        || type > static_cast<int>(JointType::Point)
        || !ParseField(fields, "joint.connectedNodeStableId", joint.connectedNodeStableId)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "joint.anchor", joint.anchor)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "joint.connectedAnchor", joint.connectedAnchor)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "joint.axis", joint.axis)
        || !ParseField(fields, "joint.minLimit", joint.minLimit)
        || !ParseField(fields, "joint.maxLimit", joint.maxLimit)
        || !ParseOptionalBool(fields, "joint.enableLimit", joint.enableLimit)) {
        return false;
    }

    joint.type = static_cast<JointType>(type);
    components.joint = joint;
    return true;
}

[[nodiscard]] bool ParseBehaviour(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasBehaviour = false;
    if (!ParseOptionalComponentFlag(fields, "behaviour", hasBehaviour)) {
        return false;
    }
    if (!hasBehaviour) {
        return true;
    }

    int backend = 0;
    int tickGroup = 0;
    BehaviourComponent behaviour;
    if (!ParseField(fields, "behaviour.behaviourAssetId", behaviour.behaviourAssetId)
        || !ParseField(fields, "behaviour.backend", backend)
        || backend < static_cast<int>(BehaviourBackend::Native)
        || backend > static_cast<int>(BehaviourBackend::VisualGraph)
        || !ParseOptionalBool(fields, "behaviour.enabled", behaviour.enabled)
        || !ParseField(fields, "behaviour.tickGroup", tickGroup)
        || tickGroup < static_cast<int>(BehaviourTickGroup::Input)
        || tickGroup > static_cast<int>(BehaviourTickGroup::Presentation)
        || !ParseField(fields, "behaviour.executionOrder", behaviour.executionOrder)) {
        return false;
    }

    behaviour.backend = static_cast<BehaviourBackend>(backend);
    behaviour.tickGroup = static_cast<BehaviourTickGroup>(tickGroup);
    components.behaviour = behaviour;
    return true;
}

[[nodiscard]] bool ParseAudioSource(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasAudioSource = false;
    if (!ParseOptionalComponentFlag(fields, "audioSource", hasAudioSource)) {
        return false;
    }
    if (!hasAudioSource) {
        return true;
    }

    int attenuationModel = 0;
    AudioSourceComponent audioSource;
    if (!ParseField(fields, "audioSource.clipAssetId", audioSource.clipAssetId)
        || !ParseField(fields, "audioSource.volume", audioSource.volume)
        || !ParseField(fields, "audioSource.pitch", audioSource.pitch)
        || !ParseOptionalBool(fields, "audioSource.loop", audioSource.loop)
        || !ParseOptionalBool(fields, "audioSource.spatial", audioSource.spatial)
        || !ParseOptionalBool(fields, "audioSource.autoplay", audioSource.autoplay)
        || !ParseOptionalBool(fields, "audioSource.enabled", audioSource.enabled)
        || !ParseOptionalBool(fields, "audioSource.mute", audioSource.mute)
        || !ParseOptionalField(fields, "audioSource.pan", audioSource.pan)
        || !ParseOptionalField(fields, "audioSource.spatialBlend", audioSource.spatialBlend)
        || !ParseField(fields, "audioSource.attenuationModel", attenuationModel)
        || attenuationModel < static_cast<int>(kb::audio::AudioAttenuationModel::None)
        || attenuationModel > static_cast<int>(kb::audio::AudioAttenuationModel::Exponential)
        || !ParseOptionalField(fields, "audioSource.minDistance", audioSource.minDistance)
        || !ParseOptionalField(fields, "audioSource.maxDistance", audioSource.maxDistance)
        || !ParseOptionalField(fields, "audioSource.rolloff", audioSource.rolloff)
        || !ParseOptionalField(fields, "audioSource.dopplerFactor", audioSource.dopplerFactor)) {
        return false;
    }

    audioSource.attenuationModel = static_cast<kb::audio::AudioAttenuationModel>(attenuationModel);
    // LIB-147: optional mixer-routing bus token (absent in every pre-LIB-147 prefab -
    // backward compatible, the default empty token routes to the implicit master).
    if (const auto outputBusField = fields.find("audioSource.outputBus"); outputBusField != fields.end()) {
        SetAudioSourceOutputBus(audioSource, outputBusField->second);
    }
    components.audioSource = audioSource;
    return true;
}

[[nodiscard]] bool ParseAudioListener(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasAudioListener = false;
    if (!ParseOptionalComponentFlag(fields, "audioListener", hasAudioListener)) {
        return false;
    }
    if (!hasAudioListener) {
        return true;
    }

    AudioListenerComponent audioListener;
    if (!ParseOptionalBool(fields, "audioListener.primary", audioListener.primary)
        || !ParseOptionalBool(fields, "audioListener.enabled", audioListener.enabled)) {
        return false;
    }

    components.audioListener = audioListener;
    return true;
}

[[nodiscard]] bool ParseAnimator(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasAnimator = false;
    if (!ParseOptionalComponentFlag(fields, "animator", hasAnimator)) return false;
    if (!hasAnimator) return true;
    Animator animator{};
    int rootMotionOwner = static_cast<int>(AnimatorRootMotionOwner::None);
    if (!ParseField(fields, "animator.controllerAssetId", animator.controllerAssetId) ||
        !ParseOptionalField(fields, "animator.speed", animator.speed) ||
        !ParseOptionalBool(fields, "animator.enabled", animator.enabled) ||
        !ParseOptionalField(fields, "animator.rootMotionOwner", rootMotionOwner) ||
        !std::isfinite(animator.speed) || animator.speed < 0.0F ||
        rootMotionOwner < static_cast<int>(AnimatorRootMotionOwner::None) ||
        rootMotionOwner > static_cast<int>(AnimatorRootMotionOwner::Rigidbody)) return false;
    animator.rootMotionOwner = static_cast<AnimatorRootMotionOwner>(rootMotionOwner);
    components.animator = animator;
    return true;
}

[[nodiscard]] bool ParseUIDocument(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasDocument = false;
    if (!ParseOptionalComponentFlag(fields, "uiDocument", hasDocument)) return false;
    if (!hasDocument) return true;
    UIDocumentComponent document{};
    if (!ParseField(fields, "uiDocument.documentAssetId", document.documentAssetId) ||
        !ParseOptionalBool(fields, "uiDocument.enabled", document.enabled)) return false;
    components.uiDocument = document;
    return true;
}

} // namespace

bool ScenePrefabAssetComponentParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    return ScenePrefabAssetCameraParser::Parse(fields, components)
        && ScenePrefabAssetMeshRendererParser::Parse(fields, components)
        && ScenePrefabAssetLightParser::Parse(fields, components)
        && ScenePrefabAssetInputParser::Parse(fields, components)
        && ParseRigidbody(fields, components)
        && ParseCollider(fields, components)
        && ParseRegionShape(fields, components)
        && ParseGuideCurve(fields, components)
        && ParseContentInstance(fields, components)
        && ParseStreamFocus(fields, components)
        && ParseCharacterController(fields, components)
        && ParseJoint(fields, components)
        && ScenePrefabAssetTagsParser::Parse(fields, components)
        && ParseBehaviour(fields, components)
        && ParseAudioSource(fields, components)
        && ParseAudioListener(fields, components)
        && ParseAnimator(fields, components)
        && ParseUIDocument(fields, components);
}

} // namespace kb::scene
