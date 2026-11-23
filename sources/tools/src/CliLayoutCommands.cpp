#include "CliCommands.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <thread>
#include "engine/core/JsonValue.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "docking/DefaultDockWorkspace.hpp"
#include "docking/DockLayoutSerializer.hpp"
#include "settings/EditorLayoutLibrary.hpp"

namespace kb::cli {

int RunLayoutCommand(const ArgumentList& arguments, CommandIo io) {
    const auto project = arguments.Option("--project");
    if (!project || arguments.Positionals().size() != 1U) {
        io.err << "error: layout list|save|restore|reset --project <dir> [--name <name>]\n";
        return 1;
    }
    std::string error;
    const auto file = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (file.empty()) { io.err << "error: " << error << '\n'; return 1; }
    const auto root = file.parent_path();
    const auto action = arguments.Positionals().front();
    if (action == "list") {
        kb::core::JsonValue data = DataObject("21kb.cli.layout-list/v1");
        auto layouts = kb::core::JsonValue::MakeArray();
        for (const auto& name : kb::editor::EditorLayoutLibrary::List(root)) {
            io.out << name << '\n';
            layouts.Append(kb::core::JsonValue::MakeString(ToValidUtf8(name)));
        }
        data.Set("layouts", std::move(layouts));
        PublishData(io, std::move(data));
        return 0;
    }
    const auto settingsPath = kb::editor::EditorConfigurationStore::FilePath(root);
    auto loaded = kb::editor::EditorConfigurationStore::Load(settingsPath, root);
    if (!loaded.Succeeded()) { io.err << "error: " << loaded.error << '\n'; return 1; }
    auto& configuration = loaded.configuration;
    kb::editor::EditorLayoutPreset preset;
    std::uint32_t next = 1U;
    const auto panels = kb::editor::DefaultDockWorkspace{}.CreatePanels();
    const auto defaultTree = kb::editor::DefaultDockWorkspace{}.CreateRoot(next);
    preset.tree = kb::editor::DockLayoutSerializer::Serialize(defaultTree.get());
    std::vector<std::uint32_t> known;
    for (const auto& panel : panels) {
        known.push_back(panel.id);
        preset.panels.push_back({.panelId = panel.id, .visible = panel.visible,
            .area = panel.area, .floatingRect = panel.floatingRect});
    }
    const auto name = arguments.Option("--name");
    if (action == "save") {
        if (!configuration.layout.empty()) preset.tree = configuration.layout;
        if (!configuration.panels.empty()) preset.panels = configuration.panels;
        if (!name || !kb::editor::DockLayoutSerializer::Parse(preset.tree, known, next) ||
            !kb::editor::EditorLayoutLibrary::Save(root, *name, preset, error)) {
            io.err << "error: " << (error.empty() ? "layout save needs a valid workspace and --name" : error) << '\n';
            return 1;
        }
        io.out << "Layout saved: " << *name << '\n';
        return 0;
    }
    if (action == "restore") {
        const auto saved = name ? kb::editor::EditorLayoutLibrary::Load(root, *name) : std::nullopt;
        if (!saved || !kb::editor::DockLayoutSerializer::Parse(saved->tree, known, next)) {
            io.err << "error: layout restore needs a readable, valid --name\n";
            return 1;
        }
        preset = *saved;
    } else if (action != "reset") {
        io.err << "error: unknown layout action\n";
        return 1;
    }
    configuration.layout = preset.tree;
    configuration.panels = preset.panels;
    configuration.layoutName = action == "reset" ? std::string{} : *name;
    if (!kb::editor::EditorConfigurationStore::Save(settingsPath, root, configuration, error)) {
        io.err << "error: " << error << '\n'; return 1;
    }
    io.out << "Layout " << (action == "reset" ? "reset" : "restored") << " for the next editor session.\n";
    return 0;
}

// Asks the editor that has the project open to do something the person should see: open a scene, or
// build the game for a platform with its Build Game panel (whose settings, keys and tools it uses). The
// request is a small file under Saved/Agent/EditorRequests that the editor carries out within a second
// and removes; one left behind longer than two minutes (no editor open) is dropped. play, stop, capture and
// frames also work while the game runs and wait for the editor's reply, a file under Saved/Agent/EditorReply.
int RunEditorCommand(const ArgumentList& arguments, CommandIo io) {
    const auto project = arguments.Option("--project");
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    const auto scene = arguments.Option("--scene");
    const auto target = arguments.Option("--target");
    const bool live = action == "play" || action == "stop" || action == "capture" || action == "frames";
    if (!project || !((action == "open-scene") || (action == "build" && target) || live)) {
        io.err << "error: editor open-scene --project <dir> [--scene /Game/<path>.21kbscene]   (the scene the game starts in without --scene)\n"
                  "       editor build --project <dir> --target windows|android|web|webgpu|linux [--profile development|release]\n"
                  "       editor play|stop --project <dir>\n"
                  "       editor capture --project <dir> [--out <file.png>]\n"
                  "       editor frames --project <dir> [--seconds <1-30>]\n";
        return 1;
    }
    std::string error;
    const auto file = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (file.empty()) { io.err << "error: " << error << '\n'; return 1; }
    const auto root = file.parent_path();
    std::error_code missing;
    kb::core::JsonValue request = kb::core::JsonValue::MakeObject();
    request.Set("action", kb::core::JsonValue::MakeString(action));
    std::string done;
    // Where the editor answers a live request: named by the request's own stamp.
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto replyFile = root / "Saved" / "Agent" / "EditorReply" / (std::to_string(stamp) + ".txt");
    std::chrono::seconds replyWait{ 10 };
    std::string withoutEditor;
    if (live) {
        if (action == "capture") {
            std::filesystem::path picture = arguments.Option("--out").has_value() ? kb::assets::Utf8Path(*arguments.Option("--out"))
                                                                                  : std::filesystem::path{ "Saved/Agent/Captures/editor-view.png" };
            // A relative --out is inside the project, as for capture.
            if (picture.is_relative()) picture = root / picture;
            request.Set("path", kb::core::JsonValue::MakeString(picture.generic_string()));
            replyWait = std::chrono::seconds{ 20 };
            withoutEditor = "render a picture without the editor: kb_cli capture --project <dir>";
        } else if (action == "frames") {
            int seconds = 0;
            try {
                seconds = std::stoi(arguments.Option("--seconds").value_or("3"));
            } catch (const std::exception&) {
                // Left at 0: the range check below says what --seconds takes.
            }
            if (seconds < 1 || seconds > 30) {
                io.err << "error: --seconds takes 1 to 30, got '" << arguments.Option("--seconds").value_or("3") << "'\n";
                return 1;
            }
            request.Set("seconds", kb::core::JsonValue::MakeNumber(seconds));
            replyWait = std::chrono::seconds{ seconds + 10 };
            withoutEditor = "get the numbers from the editor: ask the person to open the project in it";
        } else {
            withoutEditor = "play the scene without a window: kb_cli run --project <dir> --frames 120";
        }
        request.Set("stamp", kb::core::JsonValue::MakeString(std::to_string(stamp)));
    } else if (action == "open-scene") {
        // Without --scene: the scene the game starts in. A scene may be named by its name (Level2) too.
        std::string sceneText = scene.value_or(
            kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(root)).settings.defaultMap);
        std::string nameError;
        sceneText = CanonicalSceneText(project, sceneText, nameError);
        if (!nameError.empty()) {
            io.err << "error: " << nameError << '\n';
            return 1;
        }
        if (!sceneText.starts_with("/Game/") || !sceneText.ends_with(".21kbscene") ||
            !std::filesystem::is_regular_file(root / "Assets" / kb::assets::Utf8Path(sceneText.substr(6U)), missing)) {
            io.err << "error: --scene takes a scene of the project as /Game/<folder>/<Name>.21kbscene, and '" << sceneText << "' is not one\n";
            return 1;
        }
        request.Set("scene", kb::core::JsonValue::MakeString(sceneText));
        done = "The editor opens " + sceneText + " (if it has the project open).";
    } else {
        // The platform as people name it, and the package target the Build Game panel lists for it.
        const std::string_view targets[][2]{ { "windows", "Windows.x64" }, { "android", "Android.ASTC.arm64" },
            { "web", "WebGL.wasm32" }, { "webgpu", "WebGPU.wasm32" }, { "linux", "Linux.x64" } };
        std::string identifier;
        for (const auto& pair : targets) {
            if (*target == pair[0]) identifier = std::string{ pair[1] };
        }
        if (identifier.empty()) {
            io.err << "error: --target takes windows, android, web, webgpu or linux, got '" << *target << "'\n";
            return 1;
        }
        request.Set("target", kb::core::JsonValue::MakeString(identifier));
        request.Set("release", kb::core::JsonValue::MakeBool(arguments.Option("--profile").value_or("development") == "release"));
        std::filesystem::remove(root / "Saved" / "Agent" / "BuildResult.txt", missing);
        done = "The editor builds the game for " + *target + " in its Build Game panel, where the person sees the progress. "
               "Saved/Agent/BuildResult.txt says Running while it builds, then Succeeded with the folder of the game or "
               "Failed with the reason; read it again until it is no longer Running (a build takes one to several minutes).";
    }
    const auto folder = root / "Saved" / "Agent" / "EditorRequests";
    std::filesystem::create_directories(folder, missing);
    const auto partial = folder / (std::to_string(stamp) + ".json.partial");
    {
        std::ofstream output{ partial, std::ios::binary | std::ios::trunc };
        output << request.Dump();
    }
    const auto requestFile = folder / (std::to_string(stamp) + ".json");
    std::filesystem::rename(partial, requestFile, missing);
    if (missing) { io.err << "error: the request could not be written: " << missing.message() << '\n'; return 1; }
    // A running editor takes the file within a second. A build is a job worth knowing about: when nobody takes the request
    // it is withdrawn and said, instead of waiting for an editor that is not there.
    if (action == "build" || live) {
        bool taken = false;
        for (int waited = 0; waited < 40 && !taken; ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
            taken = !std::filesystem::exists(requestFile, missing);
        }
        if (!taken) {
            std::filesystem::remove(requestFile, missing);
            io.err << "error: no editor took the " << action << " request in 4 seconds: the editor is not running with this project open"
                   << (live ? ". To " + withoutEditor
                            : " (or it is playing the scene). Open the project in the editor, or build without it: kb_cli build --project <dir> --platform " + *target + " --out <folder>")
                   << '\n';
            return 1;
        }
    }
    if (live) {
        for (auto waited = std::chrono::milliseconds{ 0 }; waited < replyWait && !std::filesystem::exists(replyFile, missing); waited += std::chrono::milliseconds{ 50 }) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
        }
        std::ifstream input{ replyFile, std::ios::binary };
        if (!input) {
            io.err << "error: the editor took the " << action << " request but did not answer in " << replyWait.count() << " seconds; it may be busy or waiting on a dialog\n";
            return 1;
        }
        const std::string reply{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        input.close();
        std::filesystem::remove(replyFile, missing);
        // The first line is ok or failed, the rest says what happened.
        const std::size_t split = reply.find('\n');
        const bool ok = reply.starts_with("ok");
        const std::string text = split == std::string::npos ? std::string{} : reply.substr(split + 1U);
        (ok ? io.out : io.err) << (ok ? "" : "error: ") << text << (text.ends_with('\n') ? "" : "\n");
        if (ok) {
            kb::core::JsonValue answer = DataObject("21kb.cli.editor/v1");
            answer.Set("request", std::move(request));
            answer.Set("reply", kb::core::JsonValue::MakeString(ToValidUtf8(text)));
            PublishData(io, std::move(answer));
        }
        return ok ? 0 : 1;
    }
    io.out << done << '\n';
    kb::core::JsonValue data = DataObject("21kb.cli.editor/v1");
    data.Set("request", std::move(request));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace kb::cli
