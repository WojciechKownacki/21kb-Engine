#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationRetargeting.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// animator: builds an Animator Controller - the state machine that picks a character's
// animation - without the editor. Every change is validated and saved at once, so a controller
// on disk is always one the Animator can play; `show` prints it back in the words these
// commands take.

namespace kb::cli {
namespace {

using kb::scene::AnimatorConditionMode;
using kb::scene::AnimatorController;
using kb::scene::AnimatorControllerLayer;
using kb::scene::AnimatorControllerState;
using kb::scene::AnimatorControllerTransition;
using kb::scene::AnimatorParameterDefinition;
using kb::scene::AnimatorParameterType;
using kb::scene::AnimatorTransitionCondition;

constexpr std::string_view kUsage =
    "usage: kb_cli animator <new|show|check|param|state|transition|locomotion> --project <dir> ...\n"
    "  new         --name <Name> --clip <clip> [--clip ...] [--folder Assets/Animation]\n"
    "  show|check  --controller <path> [--character <.kbskeletalmesh>]  (check: can that character play every clip?)\n"
    "  param       add|remove --controller <path> --name <Name> [--type float|int|bool|trigger] [--default <value>]\n"
    "  state       add|remove|default --controller <path> --name <State> [--clip <clip> | --blend <FloatParameter>\n"
    "              --child <threshold>=<clip> ...] [--layer <Layer>]\n"
    "  transition  add|remove --controller <path> --from <State|Any> --to <State> [--when <condition> ...]\n"
    "              [--exit-time <0..1>] [--duration <seconds>] [--layer <Layer>]\n"
    "  locomotion  --controller <path> --parameter <Name> --clip <speed>=<clip> [--clip ...] [--name Locomotion]\n"
    "              [--default yes|no]\n"
    "  conditions: Speed>0.1  Speed<0.1  Lives==3  Lives>2  Grounded  !Grounded  Grounded==false  Jump (a trigger)\n";

struct Session {
    std::filesystem::path projectRoot;
    kb::scene::Scene scene;
    std::filesystem::path file;
    std::string virtualPath;
    AnimatorController controller;
};

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return text;
}

[[nodiscard]] std::optional<float> ParseFloat(std::string_view text) {
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

[[nodiscard]] std::optional<int> ParseInt(std::string_view text) {
    int value = 0;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] bool OpenProject(Session& session, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: animator requires --project <dir>\n" << kUsage;
        return false;
    }
    session.projectRoot = kb::assets::Utf8Path(*project);
    std::string error;
    if (!MountProjectAssets(session.scene, session.projectRoot, error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool OpenController(Session& session, const ArgumentList& arguments, CommandIo io) {
    if (!OpenProject(session, arguments, io)) return false;
    const std::optional<std::string> path = arguments.Option("--controller");
    if (!path.has_value()) {
        io.err << "error: --controller <path> is required (an Animator Controller, .kbanimcontroller)\n";
        return false;
    }
    const kb::assets::AssetMetadata* metadata =
        FindAssetByFlexiblePath(session.scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*path));
    if (metadata == nullptr || metadata->type != kb::scene::kAnimatorControllerAssetType) {
        io.err << "error: '" << *path << "' is not an Animator Controller of the project (create one with kb_cli animator new)\n";
        return false;
    }
    std::string error;
    std::optional<AnimatorController> loaded = kb::scene::AnimationAssetIO::LoadController(metadata->physicalPath, &error);
    if (!loaded.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read: " << error << '\n';
        return false;
    }
    session.file = metadata->physicalPath;
    session.virtualPath = metadata->virtualPath.generic_string();
    session.controller = std::move(*loaded);
    return true;
}

[[nodiscard]] int Save(Session& session, std::string_view done, CommandIo io) {
    std::string error;
    if (!kb::scene::AnimationAssetIO::SaveController(session.file, session.controller, &error)) {
        io.err << "error: the controller would not be playable, nothing was saved: " << error << '\n';
        return 1;
    }
    io.out << done << " (" << session.virtualPath << ")\n";
    return 0;
}

// A clip of the project, by path; refuses anything else by name.
[[nodiscard]] std::optional<std::string> ClipReference(Session& session, std::string_view text, CommandIo io) {
    const kb::assets::AssetMetadata* metadata =
        FindAssetByFlexiblePath(session.scene.Assets().Manager().Registry(), kb::assets::Utf8Path(std::string{ text }));
    if (metadata == nullptr || metadata->type != kb::scene::kAnimationClipAssetType) {
        io.err << "error: '" << text << "' is not an Animation Clip (.kbanim) of the project (kb_cli import brings them in "
               << "with their character)\n";
        return std::nullopt;
    }
    return metadata->virtualPath.generic_string();
}

// The clip's name as a state: what follows the model's name (Hero_Walk -> Walk).
[[nodiscard]] std::string StateNameOfClip(std::string_view clipReference) {
    const std::string stem = std::filesystem::path{ std::string{ clipReference } }.stem().string();
    const std::size_t underscore = stem.rfind('_');
    return underscore == std::string::npos || underscore + 1U == stem.size() ? stem : stem.substr(underscore + 1U);
}

[[nodiscard]] AnimatorControllerLayer* FindLayer(Session& session, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--layer");
    if (!name.has_value()) return &session.controller.layers.front();
    for (AnimatorControllerLayer& layer : session.controller.layers) {
        if (layer.name == *name) return &layer;
    }
    io.err << "error: the controller has no layer '" << *name << "'\n";
    return nullptr;
}

[[nodiscard]] AnimatorControllerState* FindState(AnimatorControllerLayer& layer, std::string_view name) {
    const auto found = std::ranges::find_if(layer.states, [name](const AnimatorControllerState& state) { return state.name == name; });
    return found == layer.states.end() ? nullptr : &*found;
}

[[nodiscard]] const AnimatorParameterDefinition* FindParameter(const AnimatorController& controller, std::string_view name) {
    const auto found = std::ranges::find_if(controller.parameters, [name](const AnimatorParameterDefinition& parameter) {
        return parameter.name == name;
    });
    return found == controller.parameters.end() ? nullptr : &*found;
}

[[nodiscard]] std::uint64_t NextStateId(const AnimatorController& controller) {
    std::uint64_t id = 0U;
    for (const AnimatorControllerLayer& layer : controller.layers) {
        for (const AnimatorControllerState& state : layer.states) id = std::max(id, state.id);
        for (const AnimatorControllerTransition& transition : layer.transitions) id = std::max(id, transition.id);
    }
    return id + 1U;
}

// A new state's node, beside the others in the editor's graph.
void PlaceInGraph(AnimatorController& controller, std::uint64_t stateId) {
    std::int32_t x = 0;
    for (const kb::scene::AnimatorGraphNodeLayout& node : controller.graphLayout) x = std::max(x, node.positionX + 240);
    controller.graphLayout.push_back({ .stateId = stateId, .positionX = x, .positionY = 0 });
}

[[nodiscard]] std::string_view TypeName(AnimatorParameterType type) {
    switch (type) {
    case AnimatorParameterType::Bool: return "bool";
    case AnimatorParameterType::Int: return "int";
    case AnimatorParameterType::Float: return "float";
    case AnimatorParameterType::Trigger: return "trigger";
    }
    return "float";
}

[[nodiscard]] std::string ConditionText(const AnimatorTransitionCondition& condition) {
    const auto number = [](float value) {
        std::string text = std::to_string(value);
        while (text.size() > 1U && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        return text;
    };
    switch (condition.mode) {
    case AnimatorConditionMode::BoolEquals: return (condition.boolValue ? "" : "!") + condition.parameter;
    case AnimatorConditionMode::IntEquals: return condition.parameter + "==" + std::to_string(condition.intValue);
    case AnimatorConditionMode::IntGreater: return condition.parameter + ">" + std::to_string(condition.intValue);
    case AnimatorConditionMode::IntLess: return condition.parameter + "<" + std::to_string(condition.intValue);
    case AnimatorConditionMode::FloatGreater: return condition.parameter + ">" + number(condition.floatValue);
    case AnimatorConditionMode::FloatLess: return condition.parameter + "<" + number(condition.floatValue);
    case AnimatorConditionMode::TriggerSet: return condition.parameter;
    }
    return condition.parameter;
}

// One --when: Speed>0.1, Speed<0.1, Lives==3, Grounded, !Grounded, Grounded==false, Jump.
[[nodiscard]] std::optional<AnimatorTransitionCondition> ParseCondition(
    const AnimatorController& controller, std::string_view text, CommandIo io) {
    std::string name{ text };
    std::string op;
    std::string value;
    // >= and <= read as > and < : a float is never compared for equality, and an int's bound
    // moves by one.
    for (const std::string_view candidate : { "==", ">=", "<=", ">", "<" }) {
        if (const std::size_t at = name.find(candidate); at != std::string::npos) {
            op = std::string{ candidate };
            value = name.substr(at + candidate.size());
            name = name.substr(0U, at);
            break;
        }
    }
    bool negated = false;
    if (op.empty() && !name.empty() && name.front() == '!') {
        negated = true;
        name.erase(0U, 1U);
    }
    const AnimatorParameterDefinition* parameter = FindParameter(controller, name);
    if (parameter == nullptr) {
        io.err << "error: --when '" << text << "': the controller has no parameter '" << name
               << "' (add it with kb_cli animator param add)\n";
        return std::nullopt;
    }
    AnimatorTransitionCondition condition{ .parameter = name };
    const auto refuse = [&](std::string_view expected) -> std::optional<AnimatorTransitionCondition> {
        io.err << "error: --when '" << text << "': " << name << " is a " << TypeName(parameter->type) << " parameter; write "
               << expected << '\n';
        return std::nullopt;
    };
    switch (parameter->type) {
    case AnimatorParameterType::Trigger:
        if (!op.empty() || negated) return refuse(name + " (the transition fires when it is set)");
        condition.mode = AnimatorConditionMode::TriggerSet;
        return condition;
    case AnimatorParameterType::Bool:
        if (op.empty()) {
            condition.mode = AnimatorConditionMode::BoolEquals;
            condition.boolValue = !negated;
            return condition;
        }
        if (op != "==" || (value != "true" && value != "false")) return refuse(name + ", !" + name + " or " + name + "==false");
        condition.mode = AnimatorConditionMode::BoolEquals;
        condition.boolValue = value == "true";
        return condition;
    case AnimatorParameterType::Int: {
        const std::optional<int> number = ParseInt(value);
        if (op.empty() || !number.has_value()) return refuse(name + "==3, " + name + ">2 or " + name + "<1");
        condition.mode = op == "==" ? AnimatorConditionMode::IntEquals
            : op.front() == '>' ? AnimatorConditionMode::IntGreater : AnimatorConditionMode::IntLess;
        condition.intValue = op == ">=" ? *number - 1 : op == "<=" ? *number + 1 : *number;
        return condition;
    }
    case AnimatorParameterType::Float: {
        const std::optional<float> number = ParseFloat(value);
        if (op.empty() || op == "==" || !number.has_value()) return refuse(name + ">0.1 or " + name + "<0.1 (a float is compared, never equal)");
        condition.mode = op.front() == '>' ? AnimatorConditionMode::FloatGreater : AnimatorConditionMode::FloatLess;
        condition.floatValue = *number;
        return condition;
    }
    }
    return std::nullopt;
}

void PrintController(const Session& session, std::ostream& out) {
    const AnimatorController& controller = session.controller;
    out << session.virtualPath << '\n';
    out << "parameters:";
    if (controller.parameters.empty()) out << " none";
    out << '\n';
    for (const AnimatorParameterDefinition& parameter : controller.parameters) {
        out << "  " << parameter.name << " (" << TypeName(parameter.type) << ")";
        if (parameter.type == AnimatorParameterType::Float) out << " default " << parameter.floatDefault;
        if (parameter.type == AnimatorParameterType::Int) out << " default " << parameter.intDefault;
        if (parameter.type == AnimatorParameterType::Bool) out << " default " << (parameter.boolDefault ? "true" : "false");
        out << '\n';
    }
    for (const AnimatorControllerLayer& layer : controller.layers) {
        out << "layer " << layer.name << " (weight " << layer.weight << ", starts in " << layer.defaultState << ")\n";
        for (const AnimatorControllerState& state : layer.states) {
            out << "  state " << state.name << ": ";
            if (!state.clipReference.empty()) {
                out << state.clipReference;
            } else {
                out << "blend by " << state.blendParameter;
                for (const AnimatorControllerState::BlendChild& child : state.blendChildren) {
                    out << "  " << child.threshold << "=" << child.clipReference;
                }
            }
            out << '\n';
        }
        for (const AnimatorControllerTransition& transition : layer.transitions) {
            out << "  transition " << (transition.fromState == kb::scene::kAnimatorAnyState ? "Any" : transition.fromState)
                << " -> " << transition.toState;
            if (!transition.conditions.empty()) {
                out << " when";
                for (const AnimatorTransitionCondition& condition : transition.conditions) out << ' ' << ConditionText(condition);
            }
            if (transition.exitNormalizedTime >= 0.0F) out << " at exit time " << transition.exitNormalizedTime;
            out << " over " << transition.durationSeconds << " s\n";
        }
    }
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!OpenProject(session, arguments, io)) return 1;
    const std::optional<std::string> name = arguments.Option("--name");
    const std::vector<std::string> clips = arguments.Options("--clip");
    if (!name.has_value() || clips.empty()) {
        io.err << "error: animator new requires --name <Name> and at least one --clip <clip> (each becomes a state)\n";
        return 1;
    }
    if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
        io.err << "error: --name '" << *name << "': " << problem << '\n';
        return 1;
    }
    const kb::assets::ProjectAssetFolder folder = kb::assets::ResolveProjectAssetFolder(
        session.projectRoot, arguments.Option("--folder").value_or("Assets/Animation"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    session.file = folder.folder / kb::assets::Utf8Path(*name + kb::scene::kAnimatorControllerAssetExtension);
    session.virtualPath = folder.virtualPath + "/" + *name + kb::scene::kAnimatorControllerAssetExtension;
    if (std::filesystem::exists(session.file)) {
        io.err << "error: " << session.virtualPath << " already exists; change it with animator state/transition/param\n";
        return 1;
    }
    AnimatorControllerLayer layer{ .name = "Base" };
    for (const std::string& clipText : clips) {
        const std::optional<std::string> clip = ClipReference(session, clipText, io);
        if (!clip.has_value()) return 1;
        std::string stateName = StateNameOfClip(*clip);
        if (FindState(layer, stateName) != nullptr) stateName = std::filesystem::path{ *clip }.stem().string();
        const std::uint64_t id = static_cast<std::uint64_t>(layer.states.size()) + 1U;
        layer.states.push_back({ .id = id, .name = stateName, .clipReference = *clip });
        PlaceInGraph(session.controller, id);
    }
    const AnimatorControllerState* idle = FindState(layer, "Idle");
    layer.defaultState = idle != nullptr ? idle->name : layer.states.front().name;
    session.controller.layers.push_back(std::move(layer));
    std::error_code directoryError;
    std::filesystem::create_directories(session.file.parent_path(), directoryError);
    const int saved = Save(session, "created", io);
    if (saved == 0) PrintController(session, io.out);
    return saved;
}

[[nodiscard]] int Check(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!OpenController(session, arguments, io)) return 1;
    std::string error;
    bool playable = kb::scene::AnimationAssetIO::ValidateController(session.controller, &error);
    if (!playable) io.err << "error: " << error << '\n';
    // Every clip has to exist; skeletal clips name the skeleton they were made for (another rig
    // with the same bone names plays them too, retargeted).
    kb::assets::AssetRegistry& registry = session.scene.Assets().Manager().Registry();
    // --character: the skeleton the clips have to play on; clips made for another one are
    // retargeted by bone names, and how well their bones match is reported per clip.
    std::optional<kb::scene::SkeletonAsset> character;
    std::uint64_t characterSkeletonId = 0U;
    if (const std::optional<std::string> path = arguments.Option("--character"); path.has_value()) {
        const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(*path));
        if (metadata != nullptr && metadata->type == kb::scene::kSkeletalMeshAssetType) {
            const auto binding = kb::scene::SkeletalMeshAssetIO::LoadBinding(metadata->physicalPath);
            metadata = binding.has_value() ? registry.Find(kb::assets::AssetId{ binding->skeletonAssetId }) : nullptr;
        }
        if (metadata == nullptr || metadata->type != kb::scene::kSkeletonAssetType ||
            !(character = kb::scene::SkeletonAssetIO::Load(metadata->physicalPath)).has_value()) {
            io.err << "error: --character '" << *path << "' is not a Skeletal Mesh or Skeleton of the project\n";
            return 1;
        }
        characterSkeletonId = metadata->id.value;
    }
    for (const AnimatorControllerLayer& layer : session.controller.layers) {
        for (const AnimatorControllerState& state : layer.states) {
            std::vector<std::string> references{ state.clipReference };
            if (state.clipReference.empty()) {
                references.clear();
                for (const AnimatorControllerState::BlendChild& child : state.blendChildren) references.push_back(child.clipReference);
            }
            for (const std::string& reference : references) {
                const kb::assets::AssetMetadata* metadata = registry.FindByPath(kb::assets::Utf8Path(reference));
                if (metadata == nullptr || metadata->type != kb::scene::kAnimationClipAssetType) {
                    io.err << "error: state " << state.name << ": clip " << reference << " is missing\n";
                    playable = false;
                    continue;
                }
                const std::optional<kb::scene::AnimationClip> clip = kb::scene::AnimationAssetIO::LoadClip(metadata->physicalPath);
                if (!clip.has_value()) {
                    io.err << "error: state " << state.name << ": clip " << reference << " could not be read\n";
                    playable = false;
                    continue;
                }
                io.out << "  " << state.name << ": " << reference << " (" << clip->durationSeconds << " s"
                       << (clip->looping ? ", loops" : "");
                const kb::assets::AssetMetadata* skeleton = registry.Find(kb::assets::AssetId{ clip->targetSkeletonAssetId });
                if (skeleton != nullptr) io.out << ", skeleton " << skeleton->virtualPath.generic_string();
                io.out << ")\n";
                if (!character.has_value() || clip->targetSkeletonAssetId == 0U || clip->targetSkeletonAssetId == characterSkeletonId) continue;
                const std::optional<kb::scene::SkeletonAsset> source =
                    skeleton == nullptr ? std::nullopt : kb::scene::SkeletonAssetIO::Load(skeleton->physicalPath);
                kb::scene::AnimationRetargetReport report;
                std::string retargetError;
                if (!source.has_value() ||
                    !kb::scene::RetargetAnimationClip(*clip, *source, *character, characterSkeletonId,
                        kb::scene::SkeletonCompatibilitySignature(*character), &report, &retargetError).has_value()) {
                    io.err << "error: state " << state.name << ": " << reference << " cannot play on the character: "
                           << (source.has_value() ? retargetError : std::string{ "its skeleton is missing" }) << '\n';
                    playable = false;
                    continue;
                }
                io.out << "    retargeted: " << report.mappedBones << " of " << report.targetBones << " character bones follow it"
                       << ", travel x" << report.translationScale;
                if (!report.unmappedAnimatedBones.empty()) {
                    io.out << "; not carried over:";
                    for (const std::string& bone : report.unmappedAnimatedBones) io.out << ' ' << bone;
                }
                io.out << '\n';
            }
        }
    }
    io.out << (playable ? "playable" : "not playable") << " (" << session.virtualPath << ")\n";
    return playable ? 0 : 1;
}

[[nodiscard]] int Param(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    Session session;
    if (!OpenController(session, arguments, io)) return 1;
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value() || name->empty() || (action != "add" && action != "remove" && action != "set")) {
        io.err << "error: animator param add|remove|set --controller <path> --name <Name> [--type float|int|bool|trigger] [--default <value>]\n";
        return 1;
    }
    std::vector<AnimatorParameterDefinition>& parameters = session.controller.parameters;
    const auto existing = std::ranges::find_if(parameters, [&](const AnimatorParameterDefinition& parameter) { return parameter.name == *name; });
    if (action == "set") {
        // The value the Animator starts with: what a character does before gameplay sets it.
        const std::optional<std::string> defaultText = arguments.Option("--default");
        if (existing == parameters.end() || !defaultText.has_value()) {
            io.err << "error: animator param set needs an existing parameter and --default <value>\n";
            return 1;
        }
        switch (existing->type) {
        case AnimatorParameterType::Float: {
            const std::optional<float> value = ParseFloat(*defaultText);
            if (!value.has_value()) { io.err << "error: --default of a float is a number\n"; return 1; }
            existing->floatDefault = *value;
            break;
        }
        case AnimatorParameterType::Int: {
            const std::optional<int> value = ParseInt(*defaultText);
            if (!value.has_value()) { io.err << "error: --default of an int is a whole number\n"; return 1; }
            existing->intDefault = *value;
            break;
        }
        case AnimatorParameterType::Bool:
            if (*defaultText != "true" && *defaultText != "false") { io.err << "error: --default of a bool is true or false\n"; return 1; }
            existing->boolDefault = *defaultText == "true";
            break;
        case AnimatorParameterType::Trigger:
            io.err << "error: a trigger has no default; gameplay sets it and a transition clears it\n";
            return 1;
        }
        return Save(session, "parameter " + *name + " starts at " + *defaultText, io);
    }
    if (action == "remove") {
        if (existing == parameters.end()) {
            io.err << "error: the controller has no parameter '" << *name << "'\n";
            return 1;
        }
        parameters.erase(existing);
        return Save(session, "removed parameter " + *name, io);
    }
    if (existing != parameters.end()) {
        io.err << "error: the controller already has a parameter '" << *name << "'\n";
        return 1;
    }
    const std::string type = Lower(arguments.Option("--type").value_or("float"));
    AnimatorParameterDefinition parameter{ .name = *name };
    const std::optional<std::string> defaultText = arguments.Option("--default");
    if (type == "float") {
        parameter.type = AnimatorParameterType::Float;
        const std::optional<float> value = defaultText.has_value() ? ParseFloat(*defaultText) : std::optional<float>{ 0.0F };
        if (!value.has_value()) { io.err << "error: --default of a float is a number\n"; return 1; }
        parameter.floatDefault = *value;
    } else if (type == "int") {
        parameter.type = AnimatorParameterType::Int;
        const std::optional<int> value = defaultText.has_value() ? ParseInt(*defaultText) : std::optional<int>{ 0 };
        if (!value.has_value()) { io.err << "error: --default of an int is a whole number\n"; return 1; }
        parameter.intDefault = *value;
    } else if (type == "bool") {
        parameter.type = AnimatorParameterType::Bool;
        if (defaultText.has_value() && *defaultText != "true" && *defaultText != "false") { io.err << "error: --default of a bool is true or false\n"; return 1; }
        parameter.boolDefault = defaultText == "true";
    } else if (type == "trigger") {
        parameter.type = AnimatorParameterType::Trigger;
        if (defaultText.has_value()) { io.err << "error: a trigger has no default; gameplay sets it and a transition clears it\n"; return 1; }
    } else {
        io.err << "error: --type takes float, int, bool or trigger\n";
        return 1;
    }
    parameters.push_back(parameter);
    return Save(session, "added parameter " + *name + " (" + type + ")", io);
}

[[nodiscard]] bool ReadBlend(Session& session, const ArgumentList& arguments, AnimatorControllerState& state, CommandIo io) {
    const std::vector<std::string> children = arguments.Options("--child");
    const AnimatorParameterDefinition* parameter = FindParameter(session.controller, state.blendParameter);
    if (parameter == nullptr || parameter->type != AnimatorParameterType::Float) {
        io.err << "error: --blend needs a float parameter of the controller; '" << state.blendParameter
               << "' is not one (add it with animator param add --type float)\n";
        return false;
    }
    if (children.size() < 2U) {
        io.err << "error: a blend needs at least two --child <threshold>=<clip>\n";
        return false;
    }
    for (const std::string& child : children) {
        const std::size_t equals = child.find('=');
        const std::optional<float> threshold = equals == std::string::npos ? std::nullopt : ParseFloat(std::string_view{ child }.substr(0U, equals));
        if (!threshold.has_value()) {
            io.err << "error: --child '" << child << "' is not <threshold>=<clip>\n";
            return false;
        }
        const std::optional<std::string> clip = ClipReference(session, std::string_view{ child }.substr(equals + 1U), io);
        if (!clip.has_value()) return false;
        state.blendChildren.push_back({ .threshold = *threshold, .clipReference = *clip });
    }
    std::ranges::sort(state.blendChildren, {}, &AnimatorControllerState::BlendChild::threshold);
    return true;
}

[[nodiscard]] int State(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    Session session;
    if (!OpenController(session, arguments, io)) return 1;
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value() || name->empty() || (action != "add" && action != "remove" && action != "default")) {
        io.err << "error: animator state add|remove|default --controller <path> --name <State> ...\n";
        return 1;
    }
    if (*name == "Any" || *name == kb::scene::kAnimatorAnyState) {
        io.err << "error: 'Any' names every state in transitions; a state needs another name\n";
        return 1;
    }
    AnimatorControllerLayer* layer = FindLayer(session, arguments, io);
    if (layer == nullptr) return 1;
    AnimatorControllerState* existing = FindState(*layer, *name);
    if (action == "default") {
        if (existing == nullptr) { io.err << "error: layer " << layer->name << " has no state '" << *name << "'\n"; return 1; }
        layer->defaultState = *name;
        return Save(session, "layer " + layer->name + " starts in " + *name, io);
    }
    if (action == "remove") {
        if (existing == nullptr) { io.err << "error: layer " << layer->name << " has no state '" << *name << "'\n"; return 1; }
        if (layer->states.size() == 1U) { io.err << "error: a layer keeps at least one state\n"; return 1; }
        const std::uint64_t id = existing->id;
        std::erase_if(layer->states, [&](const AnimatorControllerState& state) { return state.name == *name; });
        std::erase_if(layer->transitions, [&](const AnimatorControllerTransition& transition) {
            return transition.fromState == *name || transition.toState == *name;
        });
        std::erase_if(session.controller.graphLayout, [id](const kb::scene::AnimatorGraphNodeLayout& node) { return node.stateId == id; });
        if (layer->defaultState == *name) layer->defaultState = layer->states.front().name;
        return Save(session, "removed state " + *name + " and its transitions", io);
    }
    if (existing != nullptr) {
        io.err << "error: layer " << layer->name << " already has a state '" << *name << "'\n";
        return 1;
    }
    AnimatorControllerState state{ .id = NextStateId(session.controller), .name = *name };
    const std::optional<std::string> clip = arguments.Option("--clip");
    const std::optional<std::string> blend = arguments.Option("--blend");
    if (clip.has_value() == blend.has_value()) {
        io.err << "error: a state plays --clip <clip> or --blend <FloatParameter> --child <threshold>=<clip> ... (one of them)\n";
        return 1;
    }
    if (clip.has_value()) {
        const std::optional<std::string> reference = ClipReference(session, *clip, io);
        if (!reference.has_value()) return 1;
        state.clipReference = *reference;
    } else {
        state.blendParameter = *blend;
        if (!ReadBlend(session, arguments, state, io)) return 1;
    }
    layer->states.push_back(state);
    PlaceInGraph(session.controller, state.id);
    return Save(session, "added state " + *name, io);
}

[[nodiscard]] int Transition(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    Session session;
    if (!OpenController(session, arguments, io)) return 1;
    std::optional<std::string> from = arguments.Option("--from");
    const std::optional<std::string> to = arguments.Option("--to");
    if (!from.has_value() || !to.has_value() || (action != "add" && action != "remove")) {
        io.err << "error: animator transition add|remove --controller <path> --from <State|Any> --to <State> ...\n";
        return 1;
    }
    if (Lower(*from) == "any") from = std::string{ kb::scene::kAnimatorAnyState };
    AnimatorControllerLayer* layer = FindLayer(session, arguments, io);
    if (layer == nullptr) return 1;
    const std::string fromText = *from == kb::scene::kAnimatorAnyState ? "Any" : *from;
    if (action == "remove") {
        const std::size_t removed = std::erase_if(layer->transitions, [&](const AnimatorControllerTransition& transition) {
            return transition.fromState == *from && transition.toState == *to;
        });
        if (removed == 0U) { io.err << "error: layer " << layer->name << " has no transition " << fromText << " -> " << *to << '\n'; return 1; }
        return Save(session, "removed transition " + fromText + " -> " + *to, io);
    }
    for (const std::string& state : { *from, *to }) {
        if (state != kb::scene::kAnimatorAnyState && FindState(*layer, state) == nullptr) {
            io.err << "error: layer " << layer->name << " has no state '" << state << "'\n";
            return 1;
        }
    }
    AnimatorControllerTransition transition{ .id = NextStateId(session.controller), .fromState = *from, .toState = *to };
    if (const std::optional<std::string> duration = arguments.Option("--duration")) {
        const std::optional<float> value = ParseFloat(*duration);
        if (!value.has_value() || *value < 0.0F) { io.err << "error: --duration takes seconds, 0 or more\n"; return 1; }
        transition.durationSeconds = *value;
    } else {
        transition.durationSeconds = 0.15F;
    }
    if (const std::optional<std::string> exit = arguments.Option("--exit-time")) {
        const std::optional<float> value = ParseFloat(*exit);
        if (!value.has_value() || *value < 0.0F || *value > 1.0F) {
            io.err << "error: --exit-time is the share of the source state played, 0 to 1 (1 = when it ends)\n";
            return 1;
        }
        transition.exitNormalizedTime = *value;
    }
    for (const std::string& text : arguments.Options("--when")) {
        const std::optional<AnimatorTransitionCondition> condition = ParseCondition(session.controller, text, io);
        if (!condition.has_value()) return 1;
        transition.conditions.push_back(*condition);
    }
    if (transition.conditions.empty() && transition.exitNormalizedTime < 0.0F) {
        io.err << "error: a transition needs --when <condition> or --exit-time <0..1> (or both) to know when to fire\n";
        return 1;
    }
    layer->transitions.push_back(transition);
    return Save(session, "added transition " + fromText + " -> " + *to, io);
}

[[nodiscard]] int Locomotion(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!OpenController(session, arguments, io)) return 1;
    const std::optional<std::string> parameterName = arguments.Option("--parameter");
    const std::vector<std::string> clips = arguments.Options("--clip");
    if (!parameterName.has_value() || clips.size() < 2U) {
        io.err << "error: animator locomotion --controller <path> --parameter <Name> --clip <speed>=<clip> --clip <speed>=<clip> ...\n";
        return 1;
    }
    if (const AnimatorParameterDefinition* existing = FindParameter(session.controller, *parameterName)) {
        if (existing->type != AnimatorParameterType::Float) {
            io.err << "error: '" << *parameterName << "' is a " << TypeName(existing->type) << " parameter; locomotion blends by a float\n";
            return 1;
        }
    } else {
        session.controller.parameters.push_back({ .name = *parameterName, .type = AnimatorParameterType::Float });
    }
    AnimatorControllerLayer* layer = FindLayer(session, arguments, io);
    if (layer == nullptr) return 1;
    const std::string name = arguments.Option("--name").value_or("Locomotion");
    if (FindState(*layer, name) != nullptr) {
        io.err << "error: layer " << layer->name << " already has a state '" << name << "'\n";
        return 1;
    }
    AnimatorControllerState state{ .id = NextStateId(session.controller), .name = name, .blendParameter = *parameterName };
    for (const std::string& child : clips) {
        const std::size_t equals = child.find('=');
        const std::optional<float> threshold = equals == std::string::npos ? std::nullopt : ParseFloat(std::string_view{ child }.substr(0U, equals));
        if (!threshold.has_value()) {
            io.err << "error: --clip '" << child << "' is not <speed>=<clip> (the speed at which this clip plays alone)\n";
            return 1;
        }
        const std::optional<std::string> clip = ClipReference(session, std::string_view{ child }.substr(equals + 1U), io);
        if (!clip.has_value()) return 1;
        state.blendChildren.push_back({ .threshold = *threshold, .clipReference = *clip });
    }
    std::ranges::sort(state.blendChildren, {}, &AnimatorControllerState::BlendChild::threshold);
    layer->states.push_back(state);
    PlaceInGraph(session.controller, state.id);
    if (Lower(arguments.Option("--default").value_or("yes")) != "no") layer->defaultState = name;
    return Save(session, "added " + name + ": blends its clips by " + *parameterName +
        " (set it from gameplay: Animator.SetFloat(\"" + *parameterName + "\", speed))", io);
}

} // namespace

int RunAnimatorCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string command = positionals.empty() ? std::string{} : positionals.front();
    if (command == "new") return New(arguments, io);
    if (command == "check") return Check(arguments, io);
    if (command == "param") return Param(arguments, io);
    if (command == "state") return State(arguments, io);
    if (command == "transition") return Transition(arguments, io);
    if (command == "locomotion") return Locomotion(arguments, io);
    if (command == "show") {
        Session session;
        if (!OpenController(session, arguments, io)) return 1;
        PrintController(session, io.out);
        return 0;
    }
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
