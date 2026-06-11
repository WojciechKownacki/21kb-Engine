#include "engine/modules/EngineModuleHost.hpp"

#include "engine/modules/EngineModuleContext.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kb::modules {
namespace {

struct ResolveEntry {
    IEngineModule* module = nullptr;
    EngineModuleMetadata metadata;
    std::size_t insertionIndex = 0;
    bool placed = false;
};

using CreateEngineModuleFn = IEngineModule* (*)();
using DestroyEngineModuleFn = void (*)(IEngineModule*);

class DynamicEngineModule final : public IEngineModule {
public:
    DynamicEngineModule(
        std::filesystem::path path,
        void* library,
        IEngineModule* module,
        DestroyEngineModuleFn destroy) noexcept
        : path_(std::move(path))
        , library_(library)
        , module_(module)
        , destroy_(destroy) {}

    ~DynamicEngineModule() override {
        if (module_ != nullptr && destroy_ != nullptr) {
            destroy_(module_);
            module_ = nullptr;
        }
        UnloadLibraryHandle();
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
    void UnloadLibraryHandle() noexcept {
        if (library_ == nullptr) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(library_));
#else
        dlclose(library_);
#endif
        library_ = nullptr;
    }

    std::filesystem::path path_;
    void* library_ = nullptr;
    IEngineModule* module_ = nullptr;
    DestroyEngineModuleFn destroy_ = nullptr;
};

[[nodiscard]] std::unique_ptr<IEngineModule> LoadDynamicModule(
    const std::filesystem::path& path,
    std::vector<std::string>& diagnostics) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.wstring().c_str());
    if (library == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' could not be loaded");
        return nullptr;
    }
    auto* create = reinterpret_cast<CreateEngineModuleFn>(GetProcAddress(library, "kbCreateEngineModule"));
    auto* destroy = reinterpret_cast<DestroyEngineModuleFn>(GetProcAddress(library, "kbDestroyEngineModule"));
#else
    void* library = dlopen(path.string().c_str(), RTLD_NOW);
    if (library == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' could not be loaded");
        return nullptr;
    }
    auto* create = reinterpret_cast<CreateEngineModuleFn>(dlsym(library, "kbCreateEngineModule"));
    auto* destroy = reinterpret_cast<DestroyEngineModuleFn>(dlsym(library, "kbDestroyEngineModule"));
#endif
    if (create == nullptr || destroy == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' is missing engine module exports");
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return nullptr;
    }

    IEngineModule* module = create();
    if (module == nullptr) {
        diagnostics.push_back("plugin '" + path.string() + "' did not create a module");
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return nullptr;
    }

    return std::make_unique<DynamicEngineModule>(path, library, module, destroy);
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

void EngineModuleHost::LoadProjectPluginModules() {
    for (const kb::project::ProjectPluginReference& plugin : project_.plugins) {
        if (!plugin.enabled || plugin.binaryPath.empty()) {
            continue;
        }
        std::unique_ptr<IEngineModule> module = LoadDynamicModule(plugin.binaryPath, diagnostics_);
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
