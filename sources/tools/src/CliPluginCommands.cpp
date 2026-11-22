#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "scene/EditorPluginCatalog.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {

namespace {

[[nodiscard]] std::string_view KindName(kb::project::ProjectPluginKind kind) noexcept {
    return kind == kb::project::ProjectPluginKind::Editor ? "editor" : "game";
}

} // namespace

// The project's plugins as the Plugins window shows them: every plugin the engine ships, whether the
// project has it on, and whether it is part of the game or an editor tool only; --enable and --disable
// switch them as that window does. The editor running on the project takes the change in.
int RunPluginsCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: plugins requires --project <dir>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: " << loaded.error << '\n';
        return 1;
    }
    std::vector<kb::project::ProjectPluginReference>& plugins = loaded.descriptor.plugins;
    const auto find = [&plugins](std::string_view id) {
        return std::ranges::find_if(plugins, [id](const kb::project::ProjectPluginReference& plugin) { return plugin.name == id; });
    };

    bool changed = false;
    for (const bool enable : { true, false }) {
        for (const std::string& id : arguments.Options(enable ? "--enable" : "--disable")) {
            const kb::editor::EditorPluginDescriptor* known = kb::editor::EditorPluginCatalog::FindById(id);
            auto plugin = find(id);
            if (plugin == plugins.end() && known == nullptr) {
                io.err << "error: there is no plugin '" << id << "'; kb_cli plugins --project <dir> lists them\n";
                return 1;
            }
            if (plugin == plugins.end()) {
                if (!enable) continue;
                plugins.push_back(kb::project::ProjectPluginReference{
                    .name = id,
                    .binaryPath = kb::editor::EditorPluginCatalog::PersistentBinaryPath(id),
                    .enabled = true,
                    .kind = known->kind,
                });
                changed = true;
                continue;
            }
            if (known != nullptr) {
                changed = kb::editor::EditorPluginCatalog::NormalizeProjectPluginReference(*plugin) || changed;
            }
            changed = changed || plugin->enabled != enable;
            plugin->enabled = enable;
        }
    }
    if (changed && !kb::project::ProjectManager::SaveProject(projectFile, loaded.descriptor)) {
        io.err << "error: the project file could not be written: " << projectFile.string() << '\n';
        return 1;
    }

    using kb::core::JsonValue;
    JsonValue data = DataObject("21kb.cli.plugins/v1");
    JsonValue rows = JsonValue::MakeArray();
    const auto add = [&](std::string_view id, std::string_view name, std::string_view description, kb::project::ProjectPluginKind kind, bool enabled) {
        io.out << (enabled ? "[on]  " : "[off] ") << id << "  (" << KindName(kind) << ")  " << name;
        if (!description.empty()) io.out << " - " << description;
        io.out << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("id", JsonValue::MakeString(std::string{ id }));
        row.Set("name", JsonValue::MakeString(std::string{ name }));
        row.Set("kind", JsonValue::MakeString(std::string{ KindName(kind) }));
        row.Set("enabled", JsonValue::MakeBool(enabled));
        rows.Append(std::move(row));
    };
    for (std::size_t index = 0; index < kb::editor::EditorPluginCatalog::Count(); ++index) {
        const kb::editor::EditorPluginDescriptor* known = kb::editor::EditorPluginCatalog::At(index);
        const auto plugin = find(known->id);
        add(known->id, known->displayName, known->description, known->kind, plugin != plugins.end() && plugin->enabled);
    }
    // Plugins of the project's own, which the engine does not ship.
    for (const kb::project::ProjectPluginReference& plugin : plugins) {
        if (kb::editor::EditorPluginCatalog::FindById(plugin.name) == nullptr) {
            add(plugin.name, plugin.name, {}, plugin.kind, plugin.enabled);
        }
    }
    data.Set("plugins", std::move(rows));
    data.Set("changed", JsonValue::MakeBool(changed));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace kb::cli
