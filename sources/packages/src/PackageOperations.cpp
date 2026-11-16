#include "PackagesInternal.hpp"

#include "kb/packages/Packages.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/mods/ModManifest.hpp"

#include <algorithm>
#include <system_error>

namespace kb::packages {
namespace {

using namespace detail;

constexpr std::string_view kRangeHint = "(write ^1.2, ~1.2.3, >=1.0 <2.0 or *)";

[[nodiscard]] std::map<std::string, ModVersion> LockedVersions(const Lock& lock) {
    std::map<std::string, ModVersion> versions;
    for (const auto& [name, locked] : lock) {
        versions.emplace(name, locked.version);
    }
    return versions;
}

[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

// Resolves, installs and saves: the project changes only when every package could be chosen and checked. `pin` names a
// package whose range becomes ^<the version chosen>.
[[nodiscard]] bool Apply(
    const fs::path& projectRoot,
    const Registry& registry,
    ProjectPackages wanted,
    const std::map<std::string, ModVersion>& preferred,
    const std::string& pin,
    ProjectState& state,
    std::string& error) {
    std::map<std::string, ModVersion> resolved;
    if (!Resolve(registry, wanted.requested, preferred, resolved, error)) {
        error += " Nothing was changed.";
        return false;
    }
    if (!pin.empty()) {
        // The text is always a valid range.
        static_cast<void>(ParseRange('^' + kb::mods::ToString(resolved.at(pin)), wanted.requested[pin]));
    }
    Lock lock = state.lock;
    if (!Install(projectRoot, registry, resolved, lock, error) || (lock != state.lock && !WriteLock(projectRoot, lock, error)) ||
        (wanted != state.config && !WriteProjectPackages(projectRoot, wanted, error))) {
        return false;
    }
    state.lock = std::move(lock);
    state.config = std::move(wanted);
    return true;
}

} // namespace

bool ReadProjectState(const fs::path& projectRoot, ProjectState& state, std::string& error) {
    return ReadProjectPackages(projectRoot, state.config, error) && ReadLock(projectRoot, state.lock, error);
}

std::string RegistryText(const fs::path& projectRoot, std::string_view location) {
    if (location.starts_with("http://") || location.starts_with("https://")) {
        return std::string{ location };
    }
    std::error_code ec;
    const fs::path path = kb::assets::Utf8Path(location);
    const fs::path relative = fs::weakly_canonical(path, ec).lexically_relative(fs::weakly_canonical(projectRoot, ec));
    return Utf8Text(relative.empty() ? path : relative);
}

bool SetRegistry(const fs::path& projectRoot, std::string_view location, ProjectState& state, std::string& error) {
    ProjectPackages wanted = state.config;
    wanted.registry = RegistryText(projectRoot, location);
    if (wanted != state.config && !WriteProjectPackages(projectRoot, wanted, error)) {
        return false;
    }
    state.config = std::move(wanted);
    return true;
}

std::vector<std::string> NeededBy(const Lock& lock, const std::string& name) {
    std::vector<std::string> by;
    for (const auto& [other, locked] : lock) {
        if (locked.dependencies.contains(name)) {
            by.push_back(other);
        }
    }
    return by;
}

bool AddPackage(
    const fs::path& projectRoot,
    const Registry& registry,
    const std::string& name,
    std::string_view range,
    bool keepRegistry,
    ProjectState& state,
    std::string& error) {
    Dependency dependency;
    if (!kb::mods::IsValidModId(name)) {
        error = "'" + name + "' is not a package name (lower case letters, digits, '.', '_' and '-')";
        return false;
    }
    if (!ParseRange(range.empty() ? "*" : range, dependency)) {
        error = "'" + std::string{ range } + "' is not a version range " + std::string{ kRangeHint };
        return false;
    }
    ProjectPackages wanted = state.config;
    wanted.requested[name] = std::move(dependency);
    if (keepRegistry) {
        wanted.registry = RegistryText(projectRoot, registry.Location());
    }
    // Packages already installed keep their version while it fits; without a range the project asks for what is chosen.
    return Apply(projectRoot, registry, std::move(wanted), LockedVersions(state.lock), range.empty() ? name : std::string{}, state, error);
}

bool UpdatePackages(const fs::path& projectRoot, const Registry& registry, const std::string& name, ProjectState& state, std::string& error) {
    // One name moves to the newest version it allows while the rest stay; no name moves them all.
    std::map<std::string, ModVersion> preferred;
    if (!name.empty()) {
        if (!state.lock.contains(name)) {
            error = "'" + name + "' is not installed";
            return false;
        }
        preferred = LockedVersions(state.lock);
        preferred.erase(name);
    }
    return Apply(projectRoot, registry, state.config, preferred, {}, state, error);
}

bool RemovePackage(const fs::path& projectRoot, const std::string& name, ProjectState& state, std::string& error) {
    if (!state.config.requested.contains(name)) {
        const std::vector<std::string> by = NeededBy(state.lock, name);
        error = by.empty() ? "'" + name + "' is not installed"
                           : "'" + name + "' was not added by the project; it is needed by " + JoinNames(by) + ". Remove those instead.";
        return false;
    }
    ProjectPackages wanted = state.config;
    wanted.requested.erase(name);
    // What stays: the packages still asked for and everything they need, by the lock.
    std::map<std::string, ModVersion> keep;
    std::vector<std::string> queue;
    for (const auto& [asked, range] : wanted.requested) {
        queue.push_back(asked);
    }
    for (std::size_t next = 0U; next < queue.size(); ++next) {
        const auto locked = state.lock.find(queue[next]);
        if (locked == state.lock.end() || !keep.emplace(queue[next], locked->second.version).second) {
            continue;
        }
        for (const auto& [dependency, range] : locked->second.dependencies) {
            queue.push_back(dependency);
        }
    }
    Lock lock = state.lock;
    if (!Prune(projectRoot, keep, lock, error) || (lock != state.lock && !WriteLock(projectRoot, lock, error)) ||
        (wanted != state.config && !WriteProjectPackages(projectRoot, wanted, error))) {
        return false;
    }
    state.lock = std::move(lock);
    state.config = std::move(wanted);
    return true;
}

LockChanges Diff(const Lock& before, const Lock& after) {
    LockChanges changes;
    for (const auto& [name, now] : after) {
        const auto old = before.find(name);
        if (old == before.end()) {
            changes.installed.emplace_back(name, now.version);
        } else if (old->second.version != now.version) {
            changes.updated.push_back({ name, old->second.version, now.version });
        }
    }
    for (const auto& [name, old] : before) {
        if (!after.contains(name)) {
            changes.removed.emplace_back(name, old.version);
        }
    }
    return changes;
}

} // namespace kb::packages
