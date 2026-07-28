#include "engine/scene/AnimationAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

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

bool ValidateClip(AnimationClip& clip) {
    if (!std::isfinite(clip.durationSeconds) || clip.durationSeconds <= 0.0F || clip.tracks.empty()) return false;
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
    return true;
}

bool ValidateController(const AnimatorController& controller) {
    if (controller.layers.empty()) return false;
    std::unordered_set<std::string> parameterNames;
    for (const AnimatorParameterDefinition& parameter : controller.parameters) {
        if (parameter.name.empty() || !parameterNames.insert(parameter.name).second ||
            !std::isfinite(parameter.floatDefault) ||
            (parameter.type == AnimatorParameterType::Trigger && parameter.boolDefault)) return false;
    }
    std::unordered_set<std::string> layerNames;
    for (const AnimatorControllerLayer& layer : controller.layers) {
        if (layer.name.empty() || !layerNames.insert(layer.name).second || layer.defaultState.empty() ||
            !std::isfinite(layer.weight) || layer.weight < 0.0F || layer.weight > 1.0F || layer.mask == 0U ||
            layer.states.empty()) return false;
        std::unordered_set<std::string> stateNames;
        bool foundDefault = false;
        for (const AnimatorControllerState& state : layer.states) {
            if (state.name.empty() || state.clipReference.empty() || !stateNames.insert(state.name).second) return false;
            foundDefault |= state.name == layer.defaultState;
        }
        if (!foundDefault) return false;
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
    return true;
}

} // namespace

std::optional<AnimationClip> AnimationAssetIO::LoadClip(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    AnimationClip clip{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
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
        } else {
            return std::nullopt;
        }
    }
    return ValidateClip(clip) ? std::optional<AnimationClip>{ std::move(clip) } : std::nullopt;
}

std::optional<AnimatorController> AnimationAssetIO::LoadController(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    AnimatorController controller{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
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
        } else {
            return std::nullopt;
        }
    }
    return ValidateController(controller) ? std::optional<AnimatorController>{ std::move(controller) } : std::nullopt;
}

bool AnimationAssetIO::SaveClip(const std::filesystem::path& path, const AnimationClip& source) {
    AnimationClip clip = source;
    if (!ValidateClip(clip)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
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
    return WriteText(path, output.str());
}

bool AnimationAssetIO::SaveController(const std::filesystem::path& path, const AnimatorController& controller) {
    if (!ValidateController(controller)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
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
    return WriteText(path, output.str());
}

} // namespace kb::scene
