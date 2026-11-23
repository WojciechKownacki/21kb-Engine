#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// animation-clip: a looping or one-shot motion of an object (the engine's AnimationClip, .kbanim: transform keys over time)
// made without the editor's Animation panel. A clip plays through an Animator Controller on an Animator component,
// so `new` with a preset and `play` make the controller and the component as well: the object moves when the scene starts.

namespace kb::cli {
namespace {

using kb::math::Quat;
using kb::math::Vec3;
using kb::scene::AnimationClip;
using kb::scene::AnimationTransformKeyframe;
using kb::scene::AnimationTransformTrack;
using kb::scene::LocalTransform;

constexpr std::string_view kUsage =
    "usage: kb_cli animation-clip <new|key|show|play> --project <dir> ...\n"
    "  new   --name <Name> [--preset bob|spin|open-close|pulse[,...]] [--entity <Object>] [--seconds 2] [--amount <n>] [--loop yes|no]\n"
    "        [--play no] [--folder Assets/Animation] [--scene <path>] [--replace]\n"
    "        (with --preset and --entity the object starts moving when the scene starts; the keys are relative to the object's own place)\n"
    "  key   --clip <path|Name> --time <s> [--entity <Object>] [--position x,y,z] [--rotation x,y,z] [--scale x,y,z] [--target Child/Path]\n"
    "  show  --clip <path|Name>\n"
    "  play  --clip <path|Name> --entity <Object> [--scene <path>]   (a controller for the clip and an Animator on the object)\n";

constexpr int kKeysPerCycle = 16;
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] std::string Num(double value) { return FormatCliNumber(value); }

[[nodiscard]] std::string Triple(Vec3 value) { return Num(value.x) + "," + Num(value.y) + "," + Num(value.z); }

struct Preset {
    std::string_view name;
    double defaultAmount;
    double defaultSeconds;
    std::string_view amountMeans;
};

constexpr Preset kPresets[]{
    { "bob", 0.25, 2.0, "how high it rises and falls, in scene units" },
    { "spin", 360.0, 4.0, "degrees of a full turn around the upright axis" },
    { "open-close", 90.0, 3.0, "how many degrees it swings open around its own upright axis" },
    { "pulse", 0.2, 1.5, "how much it grows, as a share of its size (0.2 is a fifth)" },
};

[[nodiscard]] const Preset* FindPreset(std::string_view name) {
    for (const Preset& preset : kPresets) {
        if (preset.name == name) return &preset;
    }
    return nullptr;
}

[[nodiscard]] std::string PresetList() {
    std::string list;
    for (const Preset& preset : kPresets) list += (list.empty() ? "" : ", ") + std::string{ preset.name };
    return list;
}

[[nodiscard]] std::vector<std::string> Split(std::string_view text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == ',' || c == '+') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// The transform a preset gives at `phase` (0 to 1 over one cycle), relative to the object's own transform.
[[nodiscard]] LocalTransform Apply(const Preset& preset, double amount, double phase, LocalTransform transform) {
    const double wave = std::sin(2.0 * kPi * phase);
    const double smooth = 0.5 * (1.0 - std::cos(2.0 * kPi * phase)); // 0 at both ends, 1 half way
    if (preset.name == "bob") {
        transform.position.y += static_cast<float>(amount * wave);
    } else if (preset.name == "spin") {
        transform.rotation = kb::math::Normalize(kb::math::FromEulerDegrees(Vec3{ 0.0F, static_cast<float>(amount * phase), 0.0F }) * transform.rotation);
    } else if (preset.name == "open-close") {
        transform.rotation = kb::math::Normalize(kb::math::FromEulerDegrees(Vec3{ 0.0F, static_cast<float>(amount * smooth), 0.0F }) * transform.rotation);
    } else if (preset.name == "pulse") {
        const float factor = static_cast<float>(1.0 + amount * smooth);
        transform.scale = transform.scale * factor;
    }
    return transform;
}

struct OpenClip {
    std::filesystem::path file;
    std::string virtualPath;
    AnimationClip clip;
};

[[nodiscard]] std::optional<std::string> FloatOption(const ArgumentList& arguments, std::string_view option, double& value, CommandIo io, bool& bad) {
    const std::optional<std::string> text = arguments.Option(option);
    if (!text.has_value()) return std::nullopt;
    char* end = nullptr;
    const double parsed = std::strtod(text->c_str(), &end);
    if (text->empty() || end != text->c_str() + text->size() || !std::isfinite(parsed)) {
        io.err << "error: " << option << " takes a number, got '" << *text << "'\n";
        bad = true;
        return std::nullopt;
    }
    value = parsed;
    return text;
}

[[nodiscard]] std::optional<ProjectSceneDocument> SceneOf(const ArgumentList& arguments, CommandIo io) {
    std::string error;
    std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, error);
    if (!loaded.has_value()) io.err << "error: " << error << '\n';
    return loaded;
}

[[nodiscard]] std::optional<OpenClip> LoadClip(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--clip");
    const kb::assets::AssetMetadata* metadata = nullptr;
    if (text.has_value()) {
        const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
        metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(*text));
        if (metadata == nullptr && text->find('/') == std::string::npos && text->find('\\') == std::string::npos) {
            metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path("/Game/Animation/" + *text + kb::scene::kAnimationClipAssetExtension));
        }
    }
    if (metadata == nullptr || metadata->type != kb::scene::kAnimationClipAssetType) {
        io.err << "error: --clip must name an animation clip of the project, like /Game/Animation/Door.kbanim (make one with kb_cli animation-clip new)\n";
        return std::nullopt;
    }
    std::string error;
    std::optional<AnimationClip> loaded = kb::scene::AnimationAssetIO::LoadClip(metadata->physicalPath, &error);
    if (!loaded.has_value() || loaded->targetSkeletonAssetId != 0U) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read as a clip of objects" << (error.empty() ? "" : ": " + error) << '\n';
        return std::nullopt;
    }
    return OpenClip{ metadata->physicalPath, metadata->virtualPath.generic_string(), std::move(*loaded) };
}

[[nodiscard]] bool SaveClip(const OpenClip& open, CommandIo io) {
    if (!kb::scene::AnimationAssetIO::SaveClip(open.file, open.clip)) {
        io.err << "error: " << open.virtualPath << " could not be saved (a key's time must be inside the duration, and a clip needs a key)\n";
        return false;
    }
    return true;
}

[[nodiscard]] AnimationTransformTrack* FindTrack(AnimationClip& clip, std::string_view target) {
    for (AnimationTransformTrack& track : clip.tracks) {
        if (track.targetPath == target || (target == "." && track.targetPath.empty())) return &track;
    }
    return nullptr;
}

[[nodiscard]] std::string ShowClip(const OpenClip& open) {
    std::string text = "clip " + open.virtualPath + ": " + Num(open.clip.durationSeconds) + " s, " + (open.clip.looping ? "loops" : "plays once") + "\n";
    for (const AnimationTransformTrack& track : open.clip.tracks) {
        text += "  " + (track.targetPath.empty() ? std::string{ "." } : track.targetPath) + ":\n";
        for (const AnimationTransformKeyframe& key : track.keyframes) {
            text += "    " + Num(key.timeSeconds) + " s  position " + Triple(key.transform.position) + "  rotation " +
                Triple(kb::math::ToEulerDegrees(key.transform.rotation)) + "  scale " + Triple(key.transform.scale) + "\n";
        }
    }
    return text;
}

// The controller that plays the clip (made when the project has none by that name) and an Animator on the object that uses it.
[[nodiscard]] bool PlayOnObject(const ArgumentList& arguments, const OpenClip& open, const std::string& object, std::string& note, CommandIo io) {
    const std::string project = arguments.Option("--project").value_or(std::string{});
    const std::string stem = open.file.stem().string();
    const std::string controller = "/Game/Animation/" + stem + kb::scene::kAnimatorControllerAssetExtension;
    kb::scene::Scene mounted;
    std::string error;
    if (!MountProjectAssets(mounted, kb::assets::Utf8Path(project), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    std::string message;
    if (FindAssetByFlexiblePath(mounted.Assets().Manager().Registry(), kb::assets::Utf8Path(controller)) == nullptr) {
        if (!RunSiblingCommand(&RunAnimatorCommand, { "new", "--project", project, "--name", stem, "--clip", open.virtualPath }, message)) {
            io.err << "error: the controller for the clip could not be made: " << message;
            return false;
        }
    }
    std::vector<std::string> sceneWords;
    if (const std::optional<std::string> scene = arguments.Option("--scene")) sceneWords = { "--scene", *scene };
    const auto component = [&](std::string action) {
        std::vector<std::string> words{ std::move(action), "--project", project, "--entity", object, "--type", "Animator", "--set", "controller=" + controller };
        words.insert(words.end(), sceneWords.begin(), sceneWords.end());
        return words;
    };
    if (!RunSiblingCommand(&RunComponentCommand, component("add"), message) && !RunSiblingCommand(&RunComponentCommand, component("set"), message)) {
        io.err << "error: the object " << object << " could not get the Animator: " << message;
        return false;
    }
    note = "plays on '" + object + "' when the scene starts (controller " + controller + ")";
    return true;
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    {
        std::string error;
        if (!MountProjectAssets(scene, kb::assets::Utf8Path(arguments.Option("--project").value_or(std::string{})), error)) {
            io.err << "error: " << (arguments.Option("--project").has_value() ? error : "animation-clip requires --project <dir>") << '\n';
            return 1;
        }
    }
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments, "animation-clip new", "--project <dir> --name <Name>", "Assets/Animation",
        kb::scene::kAnimationClipAssetExtension, false, io);
    if (!target.has_value()) return 1;
    std::vector<const Preset*> presets;
    for (const std::string& name : Split(arguments.Option("--preset").value_or(std::string{}))) {
        const Preset* preset = FindPreset(name);
        if (preset == nullptr) {
            io.err << "error: unknown preset '" << name << "'; presets: " << PresetList() << " (several joined with a comma: bob,spin)\n";
            return 1;
        }
        presets.push_back(preset);
    }
    bool bad = false;
    double seconds = 0.0;
    double amount = 0.0;
    const bool hasSeconds = FloatOption(arguments, "--seconds", seconds, io, bad).has_value();
    const bool hasAmount = FloatOption(arguments, "--amount", amount, io, bad).has_value();
    if (bad) return 1;
    if (hasAmount && std::abs(amount) > 100000.0) {
        io.err << "error: --amount takes a number between -100000 and 100000\n";
        return 1;
    }
    if (hasAmount && presets.size() != 1U) {
        io.err << "error: --amount belongs to one preset (" << PresetList() << "); with several presets each keeps its own size\n";
        return 1;
    }
    if (presets.empty() && hasAmount) {
        io.err << "error: --amount needs a --preset\n";
        return 1;
    }
    const std::string loopText = arguments.Option("--loop").value_or("yes");
    if (loopText != "yes" && loopText != "no") {
        io.err << "error: --loop is yes or no, got '" << loopText << "'\n";
        return 1;
    }
    const std::string object = arguments.Option("--entity").value_or(std::string{});
    if (!presets.empty() && object.empty()) {
        io.err << "error: --preset needs --entity <Object>: the object it moves (kb_cli scene-list shows the names)\n";
        return 1;
    }
    LocalTransform base;
    base.rotation = Quat{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::optional<ProjectSceneDocument> loaded;
    if (!object.empty()) {
        loaded = SceneOf(arguments, io);
        if (!loaded.has_value()) return 1;
        const std::optional<SceneObjectTransform> found = FindSceneObject(*loaded, object);
        if (!found.has_value()) {
            io.err << "error: the scene has no object named '" << object << "' (kb_cli scene-list shows the names)\n";
            return 1;
        }
        base = found->transform;
    }
    OpenClip open{ target->file, target->virtualPath, {} };
    open.clip.looping = loopText == "yes";
    open.clip.durationSeconds = 2.0F;
    AnimationTransformTrack track;
    track.targetPath = ".";
    if (presets.empty()) {
        open.clip.durationSeconds = hasSeconds ? static_cast<float>(seconds) : 2.0F;
        track.keyframes.push_back({ 0.0F, base });
    } else {
        double length = hasSeconds ? seconds : presets.front()->defaultSeconds;
        if (!hasSeconds) {
            for (const Preset* preset : presets) length = std::max(length, preset->defaultSeconds);
        }
        open.clip.durationSeconds = static_cast<float>(length);
        for (int index = 0; index <= kKeysPerCycle; ++index) {
            const double phase = static_cast<double>(index) / static_cast<double>(kKeysPerCycle);
            LocalTransform transform = base;
            for (const Preset* preset : presets) transform = Apply(*preset, hasAmount ? amount : preset->defaultAmount, phase, transform);
            track.keyframes.push_back({ static_cast<float>(length * phase), transform });
        }
    }
    if (open.clip.durationSeconds <= 0.0F || open.clip.durationSeconds > 3600.0F) {
        io.err << "error: --seconds takes a number above 0 (and at most 3600)\n";
        return 1;
    }
    open.clip.tracks.push_back(std::move(track));
    std::error_code error;
    std::filesystem::create_directories(target->file.parent_path(), error);
    if (!SaveClip(open, io)) return 1;
    io.out << "created " << open.virtualPath << ": " << Num(open.clip.durationSeconds) << " s, " << (open.clip.looping ? "loops" : "plays once") << '\n';
    if (!presets.empty() && arguments.Option("--play").value_or("yes") != "no") {
        // The new file is found by the controller only once the project knows it.
        std::string note;
        if (!PlayOnObject(arguments, open, object, note, io)) return 1;
        io.out << "it " << note << "\ncheck: kb_cli run --frames 120 ends with 0 diagnostics; kb_cli capture --frames 30 and --frames 90 show it at two times\n";
    } else {
        io.out << "next: animation-clip key --clip " << open.virtualPath << " --time <s> --position x,y,z, then animation-clip play --clip " << open.virtualPath << " --entity <Object>\n";
    }
    return 0;
}

[[nodiscard]] int Key(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    {
        std::string error;
        if (!MountProjectAssets(scene, kb::assets::Utf8Path(arguments.Option("--project").value_or(std::string{})), error)) {
            io.err << "error: " << (arguments.Option("--project").has_value() ? error : "animation-clip requires --project <dir>") << '\n';
            return 1;
        }
    }
    std::optional<OpenClip> open = LoadClip(arguments, scene, io);
    if (!open.has_value()) return 1;
    bool bad = false;
    double time = 0.0;
    if (!FloatOption(arguments, "--time", time, io, bad).has_value() || bad || time < 0.0) {
        io.err << "error: animation-clip key requires --time <seconds> (0 or more)\n";
        return 1;
    }
    const std::string target = arguments.Option("--target").value_or(".");
    AnimationTransformTrack* track = FindTrack(open->clip, target);
    // What the key does not say is what the object has now, else what the track's key at that time or the one before says.
    LocalTransform transform;
    transform.rotation = Quat{ 0.0F, 0.0F, 0.0F, 1.0F };
    if (const std::optional<std::string> object = arguments.Option("--entity")) {
        const std::optional<ProjectSceneDocument> loaded = SceneOf(arguments, io);
        if (!loaded.has_value()) return 1;
        const std::optional<SceneObjectTransform> found = FindSceneObject(*loaded, *object);
        if (!found.has_value()) {
            io.err << "error: the scene has no object named '" << *object << "' (kb_cli scene-list shows the names)\n";
            return 1;
        }
        transform = found->transform;
    }
    if (track != nullptr) {
        for (const AnimationTransformKeyframe& key : track->keyframes) {
            if (key.timeSeconds <= static_cast<float>(time) + 1.0e-4F) transform = key.transform;
        }
    }
    bool said = false;
    for (const std::string_view option : { "--position", "--rotation", "--scale" }) {
        const std::optional<std::string> text = arguments.Option(option);
        if (!text.has_value()) continue;
        const std::optional<Vec3> value = ParseVec3(*text);
        if (!value.has_value()) {
            io.err << "error: " << option << " takes x,y,z" << (option == "--rotation" ? " in degrees" : "") << ", got '" << *text << "'\n";
            return 1;
        }
        if (option == "--position") transform.position = *value;
        else if (option == "--rotation") transform.rotation = kb::math::FromEulerDegrees(*value);
        else transform.scale = *value;
        said = true;
    }
    if (!said) {
        io.err << "error: animation-clip key needs --position, --rotation or --scale\n";
        return 1;
    }
    if (track == nullptr) {
        open->clip.tracks.push_back(AnimationTransformTrack{ target, ~std::uint64_t{ 0U }, {} });
        track = &open->clip.tracks.back();
    }
    const float before = open->clip.durationSeconds;
    auto& keys = track->keyframes;
    const auto same = std::ranges::find_if(keys, [&](const AnimationTransformKeyframe& key) { return std::abs(key.timeSeconds - static_cast<float>(time)) < 1.0e-4F; });
    if (same != keys.end()) {
        same->transform = transform;
    } else {
        keys.push_back({ static_cast<float>(time), transform });
        std::ranges::sort(keys, {}, &AnimationTransformKeyframe::timeSeconds);
    }
    open->clip.durationSeconds = std::max(open->clip.durationSeconds, static_cast<float>(time));
    if (!SaveClip(*open, io)) return 1;
    io.out << "key at " << Num(time) << " s on '" << target << "': position " << Triple(transform.position) << ", rotation "
           << Triple(kb::math::ToEulerDegrees(transform.rotation)) << ", scale " << Triple(transform.scale) << '\n';
    if (open->clip.durationSeconds > before) io.out << "the clip is now " << Num(open->clip.durationSeconds) << " s long\n";
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(arguments.Option("--project").value_or(std::string{})), error)) {
        io.err << "error: " << (arguments.Option("--project").has_value() ? error : "animation-clip requires --project <dir>") << '\n';
        return 1;
    }
    const std::optional<OpenClip> open = LoadClip(arguments, scene, io);
    if (!open.has_value()) return 1;
    io.out << ShowClip(*open);
    return 0;
}

[[nodiscard]] int Play(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(arguments.Option("--project").value_or(std::string{})), error)) {
        io.err << "error: " << (arguments.Option("--project").has_value() ? error : "animation-clip requires --project <dir>") << '\n';
        return 1;
    }
    const std::optional<OpenClip> open = LoadClip(arguments, scene, io);
    if (!open.has_value()) return 1;
    const std::optional<std::string> object = arguments.Option("--entity");
    if (!object.has_value()) {
        io.err << "error: animation-clip play requires --entity <Object>: the object that plays the clip\n";
        return 1;
    }
    const std::optional<ProjectSceneDocument> loaded = SceneOf(arguments, io);
    if (!loaded.has_value()) return 1;
    if (!FindSceneObject(*loaded, *object).has_value()) {
        io.err << "error: the scene has no object named '" << *object << "' (kb_cli scene-list shows the names)\n";
        return 1;
    }
    std::string note;
    if (!PlayOnObject(arguments, *open, *object, note, io)) return 1;
    io.out << open->virtualPath << " " << note << "\ncheck: kb_cli run --frames 120 ends with 0 diagnostics\n";
    return 0;
}

} // namespace

int RunAnimationClipCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "new") return New(arguments, io);
    if (action == "key") return Key(arguments, io);
    if (action == "show") return Show(arguments, io);
    if (action == "play") return Play(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
