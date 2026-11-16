#include "project/EditorPackages.hpp"

#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <utility>

namespace kb::editor {
namespace {

namespace pk = kb::packages;

template <typename Result>
[[nodiscard]] bool Finished(std::future<Result>& job) {
    return job.valid() && job.wait_for(std::chrono::seconds{ 0 }) == std::future_status::ready;
}

[[nodiscard]] std::string Show(const std::string& name, const kb::mods::ModVersion& version) {
    return name + ' ' + kb::mods::ToString(version);
}

// "acme.a 1.0.0, acme.b 2.1.0".
template <typename Items, typename Text>
[[nodiscard]] std::string Joined(const Items& items, const Text& text) {
    std::string joined;
    for (const auto& item : items) {
        joined += (joined.empty() ? "" : ", ") + text(item);
    }
    return joined;
}

// What an operation did, as a sentence.
[[nodiscard]] std::string Describe(const pk::Lock& before, const pk::Lock& after) {
    const pk::LockChanges changes = pk::Diff(before, after);
    std::string text;
    const auto add = [&text](std::string sentence) { text += (text.empty() ? "" : " ") + std::move(sentence); };
    if (!changes.installed.empty()) {
        add(EditorLocalization::Format("packages.installed", "Installed {list}.",
            { { "list", Joined(changes.installed, [](const auto& item) { return Show(item.first, item.second); }) } }));
    }
    if (!changes.updated.empty()) {
        add(EditorLocalization::Format("packages.updated", "Updated {list}.",
            { { "list", Joined(changes.updated, [](const pk::LockChange& item) {
                  return item.name + ' ' + kb::mods::ToString(item.from) + " -> " + kb::mods::ToString(item.to);
              }) } }));
    }
    if (!changes.removed.empty()) {
        add(EditorLocalization::Format("packages.removed", "Removed {list}.",
            { { "list", Joined(changes.removed, [](const auto& item) { return Show(item.first, item.second); }) } }));
    }
    return text.empty() ? std::string{ EditorText("packages.nothingToChange", "Nothing to change.") } : text;
}

// The packages of the project and of the registry as the panel lists them.
[[nodiscard]] std::vector<EditorPackageRow> MakeRows(const pk::ProjectState& state, const pk::Registry* registry) {
    std::set<std::string> names;
    for (const auto& [name, locked] : state.lock) {
        names.insert(name);
    }
    if (registry != nullptr) {
        for (const std::string& name : registry->Names()) {
            names.insert(name);
        }
    }
    std::vector<EditorPackageRow> rows;
    for (const std::string& name : names) {
        EditorPackageRow row{ .name = name };
        const auto locked = state.lock.find(name);
        const auto asked = state.config.requested.find(name);
        row.asked = asked != state.config.requested.end();
        if (locked != state.lock.end()) {
            row.installed = kb::mods::ToString(locked->second.version);
        }
        if (registry != nullptr) {
            const std::vector<kb::mods::ModVersion> versions = registry->Versions(name);
            if (!versions.empty()) {
                row.latest = kb::mods::ToString(versions.back());
            }
            // A newer version counts when the range the project asked for allows it.
            if (locked != state.lock.end() && row.asked) {
                for (auto version = versions.rbegin(); version != versions.rend() && *version > locked->second.version; ++version) {
                    if (kb::mods::Satisfies(asked->second.range, *version)) {
                        row.updatable = true;
                        break;
                    }
                }
            }
        }
        if (locked != state.lock.end() && !row.asked) {
            row.note = EditorLocalization::Format("packages.neededBy", "Needed by {list}",
                { { "list", Joined(pk::NeededBy(state.lock, name), [](const std::string& other) { return other; }) } });
        } else if (row.updatable) {
            row.note = std::string{ EditorText("packages.updateAvailable", "A newer version can be installed") };
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace

EditorPackages& EditorPackages::Instance() {
    // Never destroyed: an operation may still be running when the process ends.
    static EditorPackages& instance = *new EditorPackages();
    return instance;
}

bool EditorPackages::Pump(const std::filesystem::path& projectRoot) {
    bool changed = false;
    if (projectRoot != root_) {
        // Another project: nothing read so far belongs to it.
        root_ = projectRoot;
        rows_.clear();
        registry_.clear();
        trust_ = EditorRegistryTrust::Unknown;
        message_.clear();
        problems_.clear();
        selected_ = kNone;
        scroll = 0;
        refreshRequested_ = true;
        changed = true;
    }
    if (Finished(job_)) {
        Outcome outcome = job_.get();
        // The choice stays on the same package when the list is read again.
        const std::string chosen = selected_ < rows_.size() ? rows_[selected_].name : std::string{};
        rows_ = std::move(outcome.rows);
        registry_ = std::move(outcome.registry);
        trust_ = outcome.trust;
        if (outcome.replacesMessage || !outcome.message.empty()) {
            message_ = std::move(outcome.message);
            messageIsError_ = outcome.isError;
        }
        if (outcome.replacesMessage) {
            problems_ = std::move(outcome.problems);
        }
        selected_ = kNone;
        for (std::size_t index = 0U; index < rows_.size() && !chosen.empty(); ++index) {
            if (rows_[index].name == chosen) {
                selected_ = index;
            }
        }
        changed = true;
    }
    if (!job_.valid() && !root_.empty() && refreshRequested_) {
        refreshRequested_ = false;
        Start(Operation::Read, {});
    }
    return changed;
}

void EditorPackages::Start(Operation operation, std::string argument) {
    if (job_.valid() || root_.empty()) {
        return;
    }
    job_ = std::async(std::launch::async, [root = root_, operation, argument = std::move(argument)]() { return Run(root, operation, argument); });
}

void EditorPackages::Add(std::string name) { Start(Operation::Add, std::move(name)); }
void EditorPackages::Update(std::string name) { Start(Operation::Update, std::move(name)); }
void EditorPackages::Remove(std::string name) { Start(Operation::Remove, std::move(name)); }
void EditorPackages::Verify() { Start(Operation::Verify, {}); }
void EditorPackages::SetRegistry(std::string location) { Start(Operation::SetRegistry, std::move(location)); }

EditorPackages::Outcome EditorPackages::Run(const std::filesystem::path& projectRoot, Operation operation, const std::string& argument) {
    Outcome outcome;
    outcome.replacesMessage = operation != Operation::Read;
    pk::ProjectState state;
    std::string error;
    const auto fail = [&outcome](std::string text) {
        outcome.message = std::move(text);
        outcome.isError = true;
    };
    if (!pk::ReadProjectState(projectRoot, state, error)) {
        fail(error);
        return outcome;
    }
    // The registry is opened for the operations that need it, and to list what it offers.
    std::optional<pk::Registry> registry;
    std::string registryError;
    const auto openRegistry = [&]() {
        if (!registry.has_value() && !state.config.registry.empty()) {
            registry = pk::Registry::Open(state.config.registry, pk::RegistryOptions{ .projectRoot = projectRoot, .trustedKeys = state.config.trustedKeys }, registryError);
        }
        return registry.has_value();
    };
    const pk::Lock before = state.lock;
    switch (operation) {
    case Operation::SetRegistry:
        if (pk::SetRegistry(projectRoot, argument, state, error)) {
            outcome.message = std::string{ EditorText("packages.registrySet", "The registry was set.") };
        } else {
            fail(error);
        }
        break;
    case Operation::Remove:
        if (pk::RemovePackage(projectRoot, argument, state, error)) {
            outcome.message = Describe(before, state.lock);
        } else {
            fail(error);
        }
        break;
    case Operation::Add:
    case Operation::Update:
        if (!openRegistry()) {
            fail(state.config.registry.empty() ? std::string{ EditorText("packages.noRegistry", "Set the package registry first.") } : registryError);
        } else if (operation == Operation::Add ? pk::AddPackage(projectRoot, *registry, argument, {}, false, state, error)
                                               : pk::UpdatePackages(projectRoot, *registry, argument, state, error)) {
            outcome.message = Describe(before, state.lock);
        } else {
            fail(error);
        }
        break;
    case Operation::Verify: {
        std::vector<pk::Problem> problems;
        if (!pk::Verify(projectRoot, state.lock, state.config.requested, problems, error)) {
            fail(error);
            break;
        }
        for (const pk::Problem& problem : problems) {
            outcome.problems.push_back(problem.what + ' ' + problem.package + (problem.file.empty() ? "" : '/' + problem.file));
        }
        outcome.message = problems.empty()
            ? EditorLocalization::Format("packages.verifyOk", "All {count} packages are as installed.", { { "count", std::to_string(state.lock.size()) } })
            : EditorLocalization::Format("packages.verifyProblems", "{count} files differ from what was installed.", { { "count", std::to_string(problems.size()) } });
        outcome.isError = !problems.empty();
        break;
    }
    case Operation::Read:
        break;
    }
    // Whatever happened, the panel shows the project and the registry as they are now.
    if (!pk::ReadProjectState(projectRoot, state, error)) {
        fail(error);
        return outcome;
    }
    outcome.registry = state.config.registry;
    if (openRegistry() || state.config.registry.empty()) {
        if (registry.has_value()) {
            outcome.trust = registry->Signed() ? EditorRegistryTrust::Signed : EditorRegistryTrust::Unsigned;
        }
        outcome.rows = MakeRows(state, registry.has_value() ? &*registry : nullptr);
    } else {
        outcome.rows = MakeRows(state, nullptr);
        if (!outcome.isError) {
            fail(registryError);
        }
    }
    return outcome;
}

} // namespace kb::editor
