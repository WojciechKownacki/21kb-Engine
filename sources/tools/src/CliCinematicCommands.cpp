#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/TimelineAssetIO.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// timeline: a short cinematic as a .kbtimeline asset (the engine's Timeline: transform keys on named objects and
// events), made without the editor's Timeline panel. `fly` makes the common one in a single step: a camera that flies
// to an object, playing when the scene starts. Playing is a small Lua script on an empty object, the way scripts use
// the Timeline functions.

namespace kb::cli {
namespace {

using kb::math::Quat;
using kb::math::Vec3;

constexpr std::string_view kUsage =
    "usage: kb_cli timeline <new|key|event|show|play|fly> --project <dir> ...\n"
    "  fly   --name <Name> --to <Object> [--entity \"Main Camera\"] [--seconds 3] [--distance 5] [--height 2] [--scene <path>]\n"
    "  new   --name <Name> [--duration 3] [--folder Assets/Cinematics] [--replace]\n"
    "  key   --timeline <path> --entity <Object> --time <s> [--position x,y,z] [--rotation x,y,z] [--look-at x,y,z|<Object>] [--scale x,y,z]\n"
    "  event --timeline <path> --time <s> --event <Name> [--entity <Object>] [--payload <text>]\n"
    "  show  --timeline <path>\n"
    "  play  --timeline <path> [--loop yes] [--scene <path>]   (plays when the scene starts)\n";

[[nodiscard]] std::string Num(double value) { return FormatCliNumber(value); }

[[nodiscard]] std::string Triple(Vec3 value) { return Num(value.x) + "," + Num(value.y) + "," + Num(value.z); }

using SceneObject = SceneObjectTransform;

[[nodiscard]] std::optional<SceneObject> FindObject(const ProjectSceneDocument& scene, std::string_view name) { return FindSceneObject(scene, name); }

[[nodiscard]] std::optional<std::filesystem::path> ProjectRoot(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: timeline requires --project <dir>\n" << kUsage;
        return std::nullopt;
    }
    const std::filesystem::path root = kb::assets::Utf8Path(*project);
    std::string error;
    if (!MountProjectAssets(scene, root, error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    return root;
}

struct OpenTimeline {
    std::filesystem::path file;
    std::string virtualPath;
    kb::scene::TimelineAsset asset;
};

[[nodiscard]] std::optional<OpenTimeline> LoadTimeline(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> path = arguments.Option("--timeline");
    const kb::assets::AssetMetadata* metadata = path.has_value()
        ? FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*path)) : nullptr;
    if (metadata == nullptr || metadata->type != kb::scene::kTimelineAssetType) {
        io.err << "error: --timeline <path> must name a timeline of the project, like /Game/Cinematics/Intro.kbtimeline (make one with kb_cli timeline new)\n";
        return std::nullopt;
    }
    std::optional<kb::scene::TimelineAsset> loaded = kb::scene::TimelineAssetIO::Load(metadata->physicalPath);
    if (!loaded.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read\n";
        return std::nullopt;
    }
    return OpenTimeline{ metadata->physicalPath, metadata->virtualPath.generic_string(), std::move(*loaded) };
}

[[nodiscard]] bool SaveTimeline(const OpenTimeline& timeline, CommandIo io) {
    if (!kb::scene::TimelineAssetIO::Save(timeline.file, timeline.asset)) {
        io.err << "error: " << timeline.virtualPath << " could not be saved (a time must be inside the duration, rotations valid)\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<double> Seconds(const ArgumentList& arguments, std::string_view option, CommandIo io) {
    const std::optional<std::string> text = arguments.Option(option);
    if (!text.has_value()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (text->empty() || end != text->c_str() + text->size() || !std::isfinite(value)) {
        io.err << "error: " << option << " takes a number of seconds, got '" << *text << "'\n";
        return std::nullopt;
    }
    return value;
}

kb::scene::TimelineBindingDefinition* FindBinding(kb::scene::TimelineAsset& asset, std::string_view name) {
    for (auto& binding : asset.bindings) {
        if (binding.name == name) return &binding;
    }
    return nullptr;
}

// A binding is named after the object it moves; "." is the owner until the start script binds the real object.
void EnsureBinding(kb::scene::TimelineAsset& asset, const std::string& name) {
    if (FindBinding(asset, name) != nullptr) return;
    // A new timeline holds one placeholder binding (an asset needs one): the first object takes its place.
    if (asset.bindings.size() == 1U && asset.bindings.front().name == "Target" && asset.transformTracks.empty() && asset.eventTracks.empty()) {
        asset.bindings.front().name = name;
        return;
    }
    asset.bindings.push_back({ name, "." });
}

void SetKey(kb::scene::TimelineAsset& asset, const std::string& binding, float time, const kb::scene::LocalTransform& transform) {
    EnsureBinding(asset, binding);
    kb::scene::TimelineTransformTrack* track = nullptr;
    for (auto& candidate : asset.transformTracks) {
        if (candidate.binding == binding) track = &candidate;
    }
    if (track == nullptr) track = &asset.transformTracks.emplace_back(kb::scene::TimelineTransformTrack{ binding, {} });
    auto& keys = track->keyframes;
    const auto same = std::ranges::find_if(keys, [&](const auto& key) { return std::abs(key.timeSeconds - time) < 1.0e-4F; });
    if (same != keys.end()) {
        same->transform = transform;
    } else {
        keys.push_back({ time, transform });
        std::ranges::sort(keys, {}, &kb::scene::TimelineTransformKeyframe::timeSeconds);
    }
    asset.durationSeconds = std::max(asset.durationSeconds, time);
}

[[nodiscard]] std::string ShowTimeline(const OpenTimeline& timeline) {
    std::string text = "timeline " + timeline.virtualPath + ": " + Num(timeline.asset.durationSeconds) + " s\n";
    for (const auto& track : timeline.asset.transformTracks) {
        text += "  " + track.binding + ":\n";
        for (const auto& key : track.keyframes) {
            text += "    " + Num(key.timeSeconds) + " s  position " + Triple(key.transform.position) + "  rotation " +
                Triple(kb::math::ToEulerDegrees(key.transform.rotation)) + "\n";
        }
    }
    for (const auto& track : timeline.asset.eventTracks) {
        for (const auto& key : track.keyframes) {
            text += "  event '" + key.name + "' on " + track.binding + " at " + Num(key.timeSeconds) + " s\n";
        }
    }
    if (timeline.asset.transformTracks.empty() && timeline.asset.eventTracks.empty()) text += "  no keys yet (timeline key / timeline event)\n";
    return text;
}

[[nodiscard]] std::string LuaText(std::string_view text) {
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
    }
    return quoted + "\"";
}

// A camera that flies must be the camera the game shows. When another primary camera ranks above it or level with it (the
// camera of a player), the flight takes a higher priority while it plays and gives it back when it is over.
struct CameraHandoff {
    bool needed = false;
    int original = 0;
    int during = 0;
};

[[nodiscard]] CameraHandoff HandoffFor(const ProjectSceneDocument& scene, std::string_view mover) {
    CameraHandoff handoff;
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = scene.document.worldPrefab.Nodes();
    std::optional<int> own;
    std::optional<int> strongestOther;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const auto& camera = nodes[index].components.camera;
        if (!camera.has_value() || !camera->primary) continue;
        if (nodes[index].name == mover || NodePath(nodes, index) == mover) {
            own = camera->priority;
        } else {
            strongestOther = std::max(strongestOther.value_or(camera->priority), camera->priority);
        }
    }
    if (own.has_value() && strongestOther.has_value() && *strongestOther >= *own) {
        handoff = { true, *own, *strongestOther + 1 };
    }
    return handoff;
}

// The script that plays the timeline when the scene starts: it makes the instance, binds each named object and plays.
[[nodiscard]] std::string PlayScript(const OpenTimeline& timeline, bool loop, const CameraHandoff& handoff) {
    // The first object is the instance's owner (what the timeline's own "." means); the others are bound by name.
    const auto& bindings = timeline.asset.bindings;
    std::string code = "-- @asset " + timeline.virtualPath + "\nfunction Ready(self, dt)\n";
    if (handoff.needed) {
        code += "    self.camera = World.FindByName(" + LuaText(bindings.front().name) + ")\n    World.SetPropertyInt(self.camera, \"Camera\", \"priority\", " +
            std::to_string(handoff.during) + ")\n";
    }
    code += "    local t = Timeline.Create(" + LuaText(timeline.virtualPath) + ", World.FindByName(" + LuaText(bindings.front().name) + "))\n";
    for (std::size_t index = 1U; index < bindings.size(); ++index) {
        code += "    Timeline.Bind(t, " + LuaText(bindings[index].name) + ", World.FindByName(" + LuaText(bindings[index].name) + "))\n";
    }
    code += std::string{ "    Timeline.SetPlayback(t, 1, " } + (loop ? "true" : "false") + ")\n    Timeline.Play(t)\n";
    if (handoff.needed) code += "    self.flight = t\n";
    code += "end\n";
    if (handoff.needed) {
        code += "function Tick(self, dt)\n    if self.flight ~= nil and not Timeline.IsPlaying(self.flight) then\n        World.SetPropertyInt(self.camera, \"Camera\", \"priority\", " +
            std::to_string(handoff.original) + ")\n        self.flight = nil\n    end\nend\n";
    }
    return code;
}

// An empty object "<Name>Player" with the start script on it, so no object of the game gets another script.
[[nodiscard]] bool AttachPlay(const ArgumentList& arguments, const OpenTimeline& timeline, bool loop, std::string& note, CommandIo io) {
    const std::string stem = timeline.file.stem().string();
    std::string error;
    const std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, error);
    if (!loaded.has_value()) {
        io.err << "error: " << error << '\n';
        return false;
    }
    const CameraHandoff handoff = HandoffFor(*loaded, timeline.asset.bindings.front().name);
    if (!AttachScriptOnObject(arguments, stem + "Player", stem + "Play", PlayScript(timeline, loop, handoff), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    note = "plays when the scene starts (script /Game/Scripts/" + stem + "Play.lua on the object " + stem + "Player)";
    if (handoff.needed) note += "; the camera takes over the view while it plays, then the scene's own camera shows again";
    return true;
}

[[nodiscard]] std::optional<Vec3> PointOrObject(const std::string& text, const ProjectSceneDocument& scene) {
    if (const std::optional<Vec3> point = ParseVec3(text)) return point;
    if (const std::optional<SceneObject> object = FindObject(scene, text)) return object->transform.position;
    return std::nullopt;
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments, "timeline new", "--project <dir> --name <Name>", "Assets/Cinematics",
        kb::scene::kTimelineAssetExtension, false, io);
    if (!target.has_value()) return 1;
    kb::scene::TimelineAsset asset;
    asset.durationSeconds = 3.0F;
    if (arguments.Option("--duration").has_value()) {
        const std::optional<double> duration = Seconds(arguments, "--duration", io);
        if (!duration.has_value()) return 1;
        asset.durationSeconds = static_cast<float>(*duration);
    }
    asset.bindings = { { "Target", "." } };
    OpenTimeline timeline{ target->file, target->virtualPath, std::move(asset) };
    std::error_code error;
    std::filesystem::create_directories(target->file.parent_path(), error);
    if (!SaveTimeline(timeline, io)) return 1;
    io.out << "created " << target->virtualPath << " (" << Num(timeline.asset.durationSeconds) << " s)\n"
           << "next: timeline key --timeline " << target->virtualPath << " --entity <Object> --time 0 --position x,y,z, then timeline play\n";
    return 0;
}

[[nodiscard]] int Key(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    std::optional<OpenTimeline> timeline = LoadTimeline(arguments, scene, io);
    if (!timeline.has_value()) return 1;
    const std::optional<double> time = Seconds(arguments, "--time", io);
    if (!time.has_value() || *time < 0.0) {
        io.err << "error: timeline key requires --time <seconds> (0 or more)\n";
        return 1;
    }
    std::string objectName = arguments.Option("--entity").value_or(std::string{});
    if (objectName.empty() && timeline->asset.bindings.size() == 1U && timeline->asset.bindings.front().name != "Target") objectName = timeline->asset.bindings.front().name;
    if (objectName.empty()) {
        io.err << "error: timeline key requires --entity <Object>: the scene object it moves (for example \"Main Camera\")\n";
        return 1;
    }
    std::string sceneError;
    const std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, sceneError);
    if (!loaded.has_value()) {
        io.err << "error: " << sceneError << '\n';
        return 1;
    }
    const std::optional<SceneObject> object = FindObject(*loaded, objectName);
    if (!object.has_value()) {
        io.err << "error: the scene has no object named '" << objectName << "' (kb_cli scene-list shows the names)\n";
        return 1;
    }
    // What the key does not say is what the object has now, or what its previous key at that time said.
    kb::scene::LocalTransform transform = object->transform;
    for (const auto& track : timeline->asset.transformTracks) {
        if (track.binding != objectName) continue;
        for (const auto& key : track.keyframes) {
            if (std::abs(key.timeSeconds - static_cast<float>(*time)) < 1.0e-4F) transform = key.transform;
        }
    }
    bool said = false;
    if (const std::optional<std::string> text = arguments.Option("--position")) {
        const std::optional<Vec3> value = ParseVec3(*text);
        if (!value.has_value()) {
            io.err << "error: --position takes x,y,z, got '" << *text << "'\n";
            return 1;
        }
        transform.position = *value;
        said = true;
    }
    if (const std::optional<std::string> text = arguments.Option("--rotation")) {
        const std::optional<Vec3> value = ParseVec3(*text);
        if (!value.has_value()) {
            io.err << "error: --rotation takes x,y,z in degrees, got '" << *text << "'\n";
            return 1;
        }
        transform.rotation = kb::math::FromEulerDegrees(*value);
        said = true;
    }
    if (const std::optional<std::string> text = arguments.Option("--look-at")) {
        const std::optional<Vec3> point = PointOrObject(*text, *loaded);
        if (!point.has_value()) {
            io.err << "error: --look-at takes x,y,z or the name of an object, got '" << *text << "'\n";
            return 1;
        }
        const Vec3 forward = *point - transform.position;
        if (kb::math::Length(forward) < 1.0e-4F) {
            io.err << "error: --look-at is where the object already is\n";
            return 1;
        }
        transform.rotation = kb::math::LookRotation(forward, Vec3{ 0.0F, 1.0F, 0.0F });
        said = true;
    }
    if (const std::optional<std::string> text = arguments.Option("--scale")) {
        const std::optional<Vec3> value = ParseVec3(*text);
        if (!value.has_value()) {
            io.err << "error: --scale takes x,y,z, got '" << *text << "'\n";
            return 1;
        }
        transform.scale = *value;
        said = true;
    }
    if (!said) {
        io.err << "error: timeline key needs --position, --rotation, --look-at or --scale\n";
        return 1;
    }
    const float before = timeline->asset.durationSeconds;
    SetKey(timeline->asset, objectName, static_cast<float>(*time), transform);
    if (!SaveTimeline(*timeline, io)) return 1;
    io.out << "key at " << Num(*time) << " s on '" << objectName << "': position " << Triple(transform.position) << ", rotation "
           << Triple(kb::math::ToEulerDegrees(transform.rotation)) << '\n';
    if (timeline->asset.durationSeconds > before) io.out << "the timeline is now " << Num(timeline->asset.durationSeconds) << " s long\n";
    return 0;
}

[[nodiscard]] int Event(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    std::optional<OpenTimeline> timeline = LoadTimeline(arguments, scene, io);
    if (!timeline.has_value()) return 1;
    const std::optional<double> time = Seconds(arguments, "--time", io);
    const std::optional<std::string> name = arguments.Option("--event");
    if (!time.has_value() || *time <= 0.0 || !name.has_value() || name->empty()) {
        io.err << "error: timeline event requires --time <seconds above 0> and --event <Name>\n";
        return 1;
    }
    std::string binding = arguments.Option("--entity").value_or(std::string{});
    if (binding.empty()) binding = timeline->asset.bindings.front().name;
    EnsureBinding(timeline->asset, binding);
    kb::scene::TimelineMarkerId nextId = 1U;
    for (const auto& marker : timeline->asset.markers) nextId = std::max(nextId, marker.id + 1U);
    for (const auto& track : timeline->asset.eventTracks) {
        for (const auto& key : track.keyframes) nextId = std::max(nextId, key.id + 1U);
    }
    kb::scene::TimelineEventTrack* track = nullptr;
    for (auto& candidate : timeline->asset.eventTracks) {
        if (candidate.binding == binding) track = &candidate;
    }
    if (track == nullptr) track = &timeline->asset.eventTracks.emplace_back(kb::scene::TimelineEventTrack{ binding, {} });
    track->keyframes.push_back({ static_cast<float>(*time), nextId, *name, arguments.Option("--payload").value_or(std::string{}) });
    std::ranges::stable_sort(track->keyframes, {}, &kb::scene::TimelineEventKeyframe::timeSeconds);
    timeline->asset.durationSeconds = std::max(timeline->asset.durationSeconds, static_cast<float>(*time));
    if (!SaveTimeline(*timeline, io)) return 1;
    io.out << "event '" << *name << "' at " << Num(*time) << " s on '" << binding << "': a script on that object hears it: in function Created(self), Events.Subscribe(\"OnTimelineEvent\", function(event) ... end) with event.args.name\n";
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const std::optional<OpenTimeline> timeline = LoadTimeline(arguments, scene, io);
    if (!timeline.has_value()) return 1;
    io.out << ShowTimeline(*timeline);
    return 0;
}

[[nodiscard]] int Play(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const std::optional<OpenTimeline> timeline = LoadTimeline(arguments, scene, io);
    if (!timeline.has_value()) return 1;
    std::string note;
    if (!AttachPlay(arguments, *timeline, arguments.Option("--loop").value_or("no") == "yes", note, io)) return 1;
    io.out << timeline->virtualPath << " " << note << "\ncheck: kb_cli run --frames 240 ends with 0 diagnostics; kb_cli capture --frames <n> shows the picture at a time\n";
    return 0;
}

// The camera flies from where it is to a place near an object, looking at it, and the timeline starts with the scene.
[[nodiscard]] int Fly(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const std::optional<std::string> to = arguments.Option("--to");
    if (!to.has_value() || arguments.Option("--name").has_value() == false) {
        io.err << "error: timeline fly requires --name <Name> --to <Object>\n" << kUsage;
        return 1;
    }
    const std::string mover = arguments.Option("--entity").value_or("Main Camera");
    double seconds = 3.0;
    double distance = 5.0;
    double height = 2.0;
    const std::vector<std::pair<std::string_view, double*>> numbers{ { "--seconds", &seconds }, { "--distance", &distance }, { "--height", &height } };
    for (const auto& [option, slot] : numbers) {
        if (arguments.Option(option).has_value()) {
            const std::optional<double> value = Seconds(arguments, option, io);
            if (!value.has_value()) return 1;
            *slot = *value;
        }
    }
    if (seconds <= 0.0 || seconds > 600.0) {
        io.err << "error: --seconds takes a number above 0\n";
        return 1;
    }
    std::string sceneError;
    const std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, sceneError);
    if (!loaded.has_value()) {
        io.err << "error: " << sceneError << '\n';
        return 1;
    }
    const std::optional<SceneObject> camera = FindObject(*loaded, mover);
    const std::optional<SceneObject> goal = FindObject(*loaded, *to);
    if (!camera.has_value() || !goal.has_value()) {
        io.err << "error: the scene has no object named '" << (camera.has_value() ? *to : mover) << "' (kb_cli scene-list shows the names)\n";
        return 1;
    }
    const Vec3 end{ goal->transform.position.x, goal->transform.position.y + static_cast<float>(height), goal->transform.position.z - static_cast<float>(distance) };
    const Vec3 lookAt{ goal->transform.position.x, goal->transform.position.y + 1.0F, goal->transform.position.z };
    kb::scene::LocalTransform last = camera->transform;
    last.position = end;
    last.rotation = kb::math::LookRotation(lookAt - end, Vec3{ 0.0F, 1.0F, 0.0F });
    // The name is one the project has not used: a timeline never replaces another unless asked.
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments, "timeline fly", "--project <dir> --name <Name> --to <Object>", "Assets/Cinematics",
        kb::scene::kTimelineAssetExtension, false, io);
    if (!target.has_value()) return 1;
    OpenTimeline timeline{ target->file, target->virtualPath, {} };
    timeline.asset.durationSeconds = static_cast<float>(seconds);
    timeline.asset.bindings = { { mover, "." } };
    SetKey(timeline.asset, mover, 0.0F, camera->transform);
    SetKey(timeline.asset, mover, static_cast<float>(seconds), last);
    std::error_code error;
    std::filesystem::create_directories(target->file.parent_path(), error);
    if (!SaveTimeline(timeline, io)) return 1;
    std::string note;
    if (!AttachPlay(arguments, timeline, false, note, io)) return 1;
    io.out << "created " << target->virtualPath << ": '" << mover << "' flies from " << Triple(camera->transform.position) << " to " << Triple(end)
           << " in " << Num(seconds) << " s, looking at '" << *to << "'; it " << note << "\n"
           << "check: kb_cli run --frames 240 ends with 0 diagnostics; kb_cli capture --frames 90 shows the picture half way\n";
    return 0;
}

} // namespace

int RunTimelineCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "new") return New(arguments, io);
    if (action == "key") return Key(arguments, io);
    if (action == "event") return Event(arguments, io);
    if (action == "show") return Show(arguments, io);
    if (action == "play") return Play(arguments, io);
    if (action == "fly") return Fly(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
