#include "engine/modules/EngineModuleHost.hpp"

#include "engine/modules/EngineModuleContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace kb::modules {
namespace {

struct ResolveEntry {
    IEngineModule* module = nullptr;
    EngineModuleMetadata metadata;
    std::size_t insertionIndex = 0;
    bool placed = false;
};

class DynamicEngineModule final : public IEngineModule {
public:
    DynamicEngineModule(
        std::filesystem::path path,
        EngineModuleLibrary library,
        IEngineModule* module,
        DestroyEngineModuleFn destroy,
        bool unloadLibraryOnShutdown) noexcept
        : path_(std::move(path))
        , library_(std::move(library))
        , module_(module)
        , destroy_(destroy)
        , unloadLibraryOnShutdown_(unloadLibraryOnShutdown) {}

    ~DynamicEngineModule() override {
        if (module_ != nullptr && destroy_ != nullptr) {
            destroy_(module_);
            module_ = nullptr;
        }
        if (unloadLibraryOnShutdown_) {
            library_.Reset();
        } else {
            library_.ReleaseWithoutUnload();
        }
    }

    [[nodiscard]] EngineModuleMetadata Metadata() const override {
        return module_->Metadata();
    }

    void OnLoad(EngineModuleContext& context) override {
        module_->OnLoad(context);
    }

    void OnEnable() override {
        module_->OnEnable();
    }

    void OnSceneAttach(kb::scene::Scene& scene) override {
        module_->OnSceneAttach(scene);
    }

    void OnSceneDetach(kb::scene::Scene& scene) override {
        module_->OnSceneDetach(scene);
    }

    void OnDisable() override {
        module_->OnDisable();
    }

    void OnUnload() override {
        module_->OnUnload();
    }

private:
    std::filesystem::path path_;
    EngineModuleLibrary library_;
    IEngineModule* module_ = nullptr;
    DestroyEngineModuleFn destroy_ = nullptr;
    bool unloadLibraryOnShutdown_ = true;
};

[[nodiscard]] std::unique_ptr<IEngineModule> LoadDynamicModule(
    const kb::project::ProjectPluginReference& plugin,
    EngineModuleLoader& loader,
    std::vector<std::string>& diagnostics) {
    const std::filesystem::path path = plugin.binaryPath;
    EngineModuleLoadResult loaded = loader.Load(EngineModuleLoadDesc{
        .key = plugin.name.empty() ? path.stem().string() : plugin.name,
        .modulePath = path,
        .shadowCopy = true,
        .shadowCopyDirectory = {},
        .shadowCopyDirectoryName = "21kb_engine_modules",
        .diagnosticLabel = "engine module plugin",
    });
    if (!loaded.Succeeded()) {
        diagnostics.insert(diagnostics.end(), loaded.errors.begin(), loaded.errors.end());
        return nullptr;
    }

    auto* abiVersion = reinterpret_cast<EngineModuleAbiVersionFn>(loaded.library.FindSymbol(kEngineModuleAbiVersionSymbol, diagnostics, "engine module plugin"));
    auto* moduleName = reinterpret_cast<EngineModuleNameFn>(loaded.library.FindSymbol(kEngineModuleNameSymbol, diagnostics, "engine module plugin"));
    auto* create = reinterpret_cast<CreateEngineModuleFn>(loaded.library.FindSymbol(kEngineModuleCreateSymbol, diagnostics, "engine module plugin"));
    auto* destroy = reinterpret_cast<DestroyEngineModuleFn>(loaded.library.FindSymbol(kEngineModuleDestroySymbol, diagnostics, "engine module plugin"));
    if (abiVersion == nullptr || moduleName == nullptr || create == nullptr || destroy == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' is missing engine module exports");
        loaded.library.Reset();
        return nullptr;
    }
    if (abiVersion() != kEngineModuleAbiVersion) {
        diagnostics.push_back("plugin '" + path.string() + "' has unsupported engine module ABI version");
        loaded.library.Reset();
        return nullptr;
    }
    const char* exportedName = moduleName();
    if (exportedName == nullptr || exportedName[0] == '\0') {
        diagnostics.push_back("plugin '" + path.string() + "' exported an empty engine module name");
        loaded.library.Reset();
        return nullptr;
    }
    if (!plugin.name.empty() && plugin.name != exportedName) {
        diagnostics.push_back("plugin '" + path.string() + "' exported module name '" + std::string{ exportedName } + "' but project requested '" + plugin.name + "'");
        loaded.library.Reset();
        return nullptr;
    }
    IEngineModule* module = create();
    if (module == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' did not create a module");
        loaded.library.Reset();
        return nullptr;
    }

    const EngineModuleMetadata metadata = module->Metadata();
    return std::make_unique<DynamicEngineModule>(loaded.loadedPath, std::move(loaded.library), module, destroy, metadata.unloadLibraryOnShutdown);
}

// Topologically order the active modules: a module never loads before a
// dependency it shares the active set with. Ties between otherwise-ready modules
// break by loading phase, then by registration order, so the result is stable and
// independent of registration order beyond what dependencies require. Missing
// dependencies and cycles are reported through `diagnostics` and degrade
// gracefully (the affected modules still load, just in phase order).
[[nodiscard]] std::vector<IEngineModule*> ResolveOrder(
    std::vector<ResolveEntry>& entries,
    std::vector<std::string>& diagnostics) {
    std::unordered_map<std::string, std::size_t> indexByName;
    indexByName.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        indexByName.emplace(entries[i].metadata.name, i);
    }

    std::vector<IEngineModule*> ordered;
    ordered.reserve(entries.size());

    const auto dependenciesSatisfied = [&](const ResolveEntry& entry) {
        for (const std::string& dependency : entry.metadata.dependencies) {
            const auto found = indexByName.find(dependency);
            if (found == indexByName.end()) {
                continue; // missing dependency: reported once below, not a blocker
            }
            if (!entries[found->second].placed) {
                return false;
            }
        }
        return true;
    };

    // Report dependencies that are not part of the active set exactly once.
    for (const ResolveEntry& entry : entries) {
        for (const std::string& dependency : entry.metadata.dependencies) {
            if (indexByName.find(dependency) == indexByName.end()) {
                diagnostics.push_back(
                    "module '" + entry.metadata.name + "' depends on inactive or unknown module '" + dependency + "'");
            }
        }
    }

    std::size_t placedCount = 0;
    while (placedCount < entries.size()) {
        ResolveEntry* next = nullptr;
        for (ResolveEntry& entry : entries) {
            if (entry.placed || !dependenciesSatisfied(entry)) {
                continue;
            }
            if (next == nullptr) {
                next = &entry;
                continue;
            }
            const bool earlierPhase = entry.metadata.loadingPhase < next->metadata.loadingPhase;
            const bool samePhaseEarlierInsertion = entry.metadata.loadingPhase == next->metadata.loadingPhase &&
                entry.insertionIndex < next->insertionIndex;
            if (earlierPhase || samePhaseEarlierInsertion) {
                next = &entry;
            }
        }

        if (next == nullptr) {
            // Remaining entries form a dependency cycle. Place them in phase order so
            // the engine still runs, and report the cycle.
            std::vector<ResolveEntry*> remaining;
            for (ResolveEntry& entry : entries) {
                if (!entry.placed) {
                    remaining.push_back(&entry);
                }
            }
            std::stable_sort(remaining.begin(), remaining.end(), [](const ResolveEntry* lhs, const ResolveEntry* rhs) {
                if (lhs->metadata.loadingPhase != rhs->metadata.loadingPhase) {
                    return lhs->metadata.loadingPhase < rhs->metadata.loadingPhase;
                }
                return lhs->insertionIndex < rhs->insertionIndex;
            });
            std::string names;
            for (ResolveEntry* entry : remaining) {
                if (!names.empty()) {
                    names += ", ";
                }
                names += entry->metadata.name;
                entry->placed = true;
                ordered.push_back(entry->module);
            }
            diagnostics.push_back("dependency cycle among modules: " + names);
            break;
        }

        next->placed = true;
        ordered.push_back(next->module);
        ++placedCount;
    }

    return ordered;
}

} // namespace

EngineModuleHost::EngineModuleHost(kb::project::ProjectDescriptor project)
    : project_(std::move(project)) {}

EngineModuleHost::~EngineModuleHost() {
    Unload();
}

void EngineModuleHost::Add(std::unique_ptr<IEngineModule> module) {
    if (loaded_ || module == nullptr) {
        return;
    }
    candidates_.push_back(std::move(module));
}

void EngineModuleHost::ClearProjectPluginModules() noexcept {
    if (!hasProjectPluginCandidates_) {
        return;
    }
    if (projectPluginCandidateStart_ < candidates_.size()) {
        candidates_.erase(candidates_.begin() + static_cast<std::ptrdiff_t>(projectPluginCandidateStart_), candidates_.end());
    }
    projectPluginCandidateStart_ = 0U;
    hasProjectPluginCandidates_ = false;
}

void EngineModuleHost::LoadProjectPluginModules() {
    ClearProjectPluginModules();
    projectPluginCandidateStart_ = candidates_.size();
    hasProjectPluginCandidates_ = true;

    for (const kb::project::ProjectPluginReference& plugin : project_.plugins) {
        if (!plugin.enabled || plugin.binaryPath.empty()) {
            continue;
        }
        std::unique_ptr<IEngineModule> module = LoadDynamicModule(plugin, moduleLoader_, diagnostics_);
        if (module != nullptr) {
            candidates_.push_back(std::move(module));
        }
    }
}

bool EngineModuleHost::IsEnabledByProject(const EngineModuleMetadata& metadata) const {
    for (const kb::project::ProjectPluginReference& plugin : project_.plugins) {
        if (plugin.name == metadata.name) {
            return plugin.enabled;
        }
    }
    return !project_.disableEnginePluginsByDefault;
}

void EngineModuleHost::Load(kb::ecs::World& world) {
    if (loaded_) {
        return;
    }
    loaded_ = true;
    diagnostics_.clear();

    LoadProjectPluginModules();

    std::vector<ResolveEntry> entries;
    entries.reserve(candidates_.size());
    for (const std::unique_ptr<IEngineModule>& candidate : candidates_) {
        EngineModuleMetadata metadata = candidate->Metadata();
        if (!IsEnabledByProject(metadata)) {
            continue;
        }
        entries.push_back(ResolveEntry{ candidate.get(), std::move(metadata), entries.size(), false });
    }

    active_ = ResolveOrder(entries, diagnostics_);

    EngineModuleContext context{ world, project_ };
    for (IEngineModule* module : active_) {
        module->OnLoad(context);
    }
    for (IEngineModule* module : active_) {
        module->OnEnable();
    }
}

void EngineModuleHost::AttachScene(kb::scene::Scene& scene) {
    for (IEngineModule* module : active_) {
        module->OnSceneAttach(scene);
    }
}

void EngineModuleHost::DetachScene(kb::scene::Scene& scene) {
    for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
        (*it)->OnSceneDetach(scene);
    }
}

void EngineModuleHost::Unload() {
    if (!loaded_) {
        return;
    }
    for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
        (*it)->OnDisable();
    }
    for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
        (*it)->OnUnload();
    }
    active_.clear();
    loaded_ = false;
}

void EngineModuleHost::Reload(kb::ecs::World& world, std::span<kb::scene::Scene*> attachedScenes) {
    if (loaded_) {
        for (auto it = attachedScenes.rbegin(); it != attachedScenes.rend(); ++it) {
            if (*it != nullptr) {
                DetachScene(**it);
            }
        }
        Unload();
    }

    ClearProjectPluginModules();
    Load(world);

    for (kb::scene::Scene* scene : attachedScenes) {
        if (scene != nullptr) {
            AttachScene(*scene);
        }
    }
}

bool EngineModuleHost::IsActive(std::string_view name) const noexcept {
    for (const IEngineModule* module : active_) {
        if (module->Metadata().name == name) {
            return true;
        }
    }
    return false;
}

std::size_t EngineModuleHost::ActiveCount() const noexcept {
    return active_.size();
}

const std::vector<std::string>& EngineModuleHost::Diagnostics() const noexcept {
    return diagnostics_;
}

} // namespace kb::modules
