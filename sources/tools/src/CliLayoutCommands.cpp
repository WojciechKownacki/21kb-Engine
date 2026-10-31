#include "CliCommands.hpp"

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
        for (const auto& name : kb::editor::EditorLayoutLibrary::List(root)) io.out << name << '\n';
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

} // namespace kb::cli
