#include "CliCommands.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include "engine/core/JsonValue.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
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
// and removes; one left behind longer than two minutes (no editor open) is dropped.
int RunEditorCommand(const ArgumentList& arguments, CommandIo io) {
    const auto project = arguments.Option("--project");
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    const auto scene = arguments.Option("--scene");
    const auto target = arguments.Option("--target");
    if (!project || !((action == "open-scene" && scene) || (action == "build" && target))) {
        io.err << "error: editor open-scene --project <dir> --scene /Game/<path>.21kbscene\n"
                  "       editor build --project <dir> --target windows|android|web|webgpu|linux [--profile development|release]\n";
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
    if (action == "open-scene") {
        if (!scene->starts_with("/Game/") || !scene->ends_with(".21kbscene") ||
            !std::filesystem::is_regular_file(root / "Assets" / kb::assets::Utf8Path(scene->substr(6U)), missing)) {
            io.err << "error: --scene takes a scene of the project as /Game/<folder>/<Name>.21kbscene, and '" << *scene << "' is not one\n";
            return 1;
        }
        request.Set("scene", kb::core::JsonValue::MakeString(*scene));
        done = "The editor opens " + *scene + " (if it has the project open).";
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
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto partial = folder / (std::to_string(stamp) + ".json.partial");
    {
        std::ofstream output{ partial, std::ios::binary | std::ios::trunc };
        output << request.Dump();
    }
    std::filesystem::rename(partial, folder / (std::to_string(stamp) + ".json"), missing);
    if (missing) { io.err << "error: the request could not be written: " << missing.message() << '\n'; return 1; }
    io.out << done << '\n';
    kb::core::JsonValue data = DataObject("21kb.cli.editor/v1");
    data.Set("request", std::move(request));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace kb::cli
