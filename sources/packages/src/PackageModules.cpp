#include "PackagesInternal.hpp"

#include "kb/packages/Packages.hpp"

#include "engine/mods/ModManifest.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <set>

namespace kb::packages {

using namespace detail;

std::string_view CurrentModulePlatform() noexcept {
#if defined(_WIN32)
    constexpr std::string_view system = "windows";
#elif defined(__ANDROID__)
    constexpr std::string_view system = "android";
#elif defined(__APPLE__)
    constexpr std::string_view system = "macos";
#else
    constexpr std::string_view system = "linux";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    constexpr std::string_view architecture = "arm64";
#else
    constexpr std::string_view architecture = "x64";
#endif
    static const std::string platform = std::string{ system } + '-' + std::string{ architecture };
    return platform;
}

bool CheckModules(const Manifest& manifest, std::string& error) {
    if (manifest.modules.empty()) {
        return true;
    }
    const std::string label = Show(manifest.name, manifest.version);
    const std::string platform{ CurrentModulePlatform() };
    std::set<std::string> others;
    bool here = false;
    for (const ModuleEntry& module : manifest.modules) {
        if (module.platform != platform) {
            others.insert(module.platform);
            continue;
        }
        here = true;
        if (module.abi != kb::modules::kEngineModuleAbiVersion) {
            error = label + " needs " + (module.abi > kb::modules::kEngineModuleAbiVersion ? "a newer" : "an older") + " version of the engine: its module " + module.name +
                " was built for module interface " + std::to_string(module.abi) + " and this engine uses " + std::to_string(kb::modules::kEngineModuleAbiVersion) + ". " +
                (module.abi > kb::modules::kEngineModuleAbiVersion ? "Update the engine" : "Ask its author for an update") + ", or choose another version of the package.";
            return false;
        }
    }
    if (!here) {
        std::string list;
        for (const std::string& other : others) {
            list += (list.empty() ? "" : ", ") + other;
        }
        error = label + " has native modules only for " + list + ", not for this computer (" + platform + ").";
        return false;
    }
    return true;
}

bool SyncProjectModules(const fs::path& projectRoot, const Lock& lock, bool& changed, std::string& error) {
    changed = false;
    // What the installed packages bring for this platform, from the manifests as installed.
    std::vector<kb::project::ProjectPluginReference> wanted;
    for (const auto& [name, locked] : lock) {
        std::string text;
        Manifest manifest;
        if (!ReadText(InstallFolder(projectRoot, name) / std::string{ kManifestFile }, kMaxManifestBytes, text, error) || !ParseManifest(text, manifest, error)) {
            error = "The installed package " + name + " cannot be read: " + error;
            return false;
        }
        for (const ModuleEntry& module : manifest.modules) {
            if (module.platform == CurrentModulePlatform()) {
                wanted.push_back({ .name = module.name, .binaryPath = std::string{ kPackagesFolder } + '/' + name + '/' + module.file, .enabled = true });
            }
        }
    }
    std::string findError;
    const fs::path file = kb::project::ProjectManager::FindProjectFile(projectRoot, findError);
    if (file.empty()) {
        if (!wanted.empty()) {
            error = findError;
        }
        return wanted.empty();
    }
    kb::project::ProjectDescriptorReadResult read = kb::project::ProjectManager::LoadProject(file);
    if (!read.succeeded) {
        error = "The project file cannot be read: " + read.error;
        return false;
    }
    const std::vector<kb::project::ProjectPluginReference> before = read.descriptor.plugins;
    const std::string prefix = std::string{ kPackagesFolder } + '/';
    std::map<std::string, bool> wasEnabled;
    std::vector<kb::project::ProjectPluginReference> plugins;
    for (kb::project::ProjectPluginReference& plugin : read.descriptor.plugins) {
        if (plugin.binaryPath.starts_with(prefix)) {
            wasEnabled.emplace(plugin.name, plugin.enabled);
        } else {
            plugins.push_back(std::move(plugin));
        }
    }
    for (kb::project::ProjectPluginReference& module : wanted) {
        if (std::ranges::any_of(plugins, [&module](const kb::project::ProjectPluginReference& own) { return own.name == module.name; })) {
            error = "The module " + module.name + " has the name of a plugin the project already has.";
            return false;
        }
        if (const auto previous = wasEnabled.find(module.name); previous != wasEnabled.end()) {
            module.enabled = previous->second;
        }
        plugins.push_back(std::move(module));
    }
    changed = !std::ranges::equal(before, plugins, [](const auto& a, const auto& b) { return a.name == b.name && a.binaryPath == b.binaryPath && a.enabled == b.enabled; });
    if (!changed) {
        return true;
    }
    if (read.legacySettings.present) {
        error = "The project file is from an older version; open the project in the editor once, then install the package again.";
        changed = false;
        return false;
    }
    read.descriptor.plugins = std::move(plugins);
    if (!kb::project::ProjectManager::SaveProject(file, read.descriptor)) {
        error = "The project file could not be written.";
        changed = false;
        return false;
    }
    return true;
}

} // namespace kb::packages
