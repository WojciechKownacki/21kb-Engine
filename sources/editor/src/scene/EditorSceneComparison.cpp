#include "scene/EditorSceneComparison.hpp"

#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

#if defined(_WIN32)
#include "project/EditorProjectRepository.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace kb::editor {
namespace {

using kb::scene::ScenePrefabNodeDesc;
using kb::script::ScriptSceneComponentApi;

constexpr std::uint32_t kNoNode = ScenePrefabNodeDesc::NoParent;

// A scene's objects with the key each one is paired by and its place among its siblings.
struct IndexedScene {
    std::span<const ScenePrefabNodeDesc> nodes;
    std::vector<std::string> keys;
    std::vector<std::uint32_t> position;
};

[[nodiscard]] IndexedScene Index(const kb::scene::ScenePrefab& prefab) {
    IndexedScene indexed{ .nodes = prefab.Nodes() };
    indexed.keys.resize(indexed.nodes.size());
    indexed.position.resize(indexed.nodes.size());
    std::unordered_map<std::uint32_t, std::uint32_t> childCounts;
    std::unordered_map<std::string, std::uint32_t> sameName;
    for (std::uint32_t index = 0U; index < indexed.nodes.size(); ++index) {
        const ScenePrefabNodeDesc& node = indexed.nodes[index];
        indexed.position[index] = childCounts[node.parentNode]++;
        // The control characters keep a name that looks like a path from reading as one.
        std::string group = std::to_string(node.parentNode) + '\x1f' + node.name;
        const std::uint32_t occurrence = sameName[std::move(group)]++;
        std::string key = node.parentNode == kNoNode ? std::string{} : indexed.keys[node.parentNode];
        key += '\x1f';
        key += node.name;
        key += '\x1e';
        key += std::to_string(occurrence);
        indexed.keys[index] = std::move(key);
    }
    return indexed;
}

[[nodiscard]] std::string PathOf(const IndexedScene& scene, std::uint32_t index) {
    std::string path;
    bool leaf = true;
    for (std::uint32_t node = index; node != kNoNode; node = scene.nodes[node].parentNode) {
        path.insert(0U, leaf ? scene.nodes[node].name : scene.nodes[node].name + '/');
        leaf = false;
    }
    return path;
}

[[nodiscard]] std::vector<std::uint32_t> AddressOf(const IndexedScene& scene, std::uint32_t index) {
    std::vector<std::uint32_t> address;
    for (std::uint32_t node = index; node != kNoNode; node = scene.nodes[node].parentNode) {
        address.push_back(scene.position[node]);
    }
    std::ranges::reverse(address);
    return address;
}

// The component fields scripts and tools read, found once for every object.
struct ComponentFields {
    std::string_view name;
    std::span<const kb::script::ScriptSceneComponentPropertyDesc> properties;
};

[[nodiscard]] std::vector<ComponentFields> AllComponentFields() {
    std::vector<ComponentFields> all;
    for (const std::string_view name : ScriptSceneComponentApi::ComponentNames()) {
        // An older name for a component is the same fields again.
        if (kb::scene::SceneComponentAuthoring::CanonicalName(name) != name) {
            continue;
        }
        all.push_back({ name, ScriptSceneComponentApi::ComponentProperties(name) });
    }
    return all;
}

void CompareObjects(const std::vector<ComponentFields>& components, kb::scene::Scene& before, kb::scene::SceneEntity beforeEntity,
    kb::scene::Scene& after, kb::scene::SceneEntity afterEntity, std::vector<EditorSceneFieldChange>& changes) {
    for (const ComponentFields& component : components) {
        const bool had = ScriptSceneComponentApi::HasComponent(before, beforeEntity, component.name);
        const bool has = ScriptSceneComponentApi::HasComponent(after, afterEntity, component.name);
        if (had != has) {
            changes.push_back({ std::string{ component.name }, {}, had ? "present" : "", has ? "present" : "" });
            continue;
        }
        if (!has) {
            continue;
        }
        for (const kb::script::ScriptSceneComponentPropertyDesc& property : component.properties) {
            // A read-only value follows from the writable ones.
            if (!property.writable) {
                continue;
            }
            const auto was = ScriptSceneComponentApi::GetProperty(before, beforeEntity, component.name, property.name);
            const auto is = ScriptSceneComponentApi::GetProperty(after, afterEntity, component.name, property.name);
            if (was.succeeded && is.succeeded && was.value != is.value) {
                changes.push_back({ std::string{ component.name }, std::string{ property.name },
                    kb::script::FormatValueText(was.value), kb::script::FormatValueText(is.value) });
            }
        }
    }
}

[[nodiscard]] bool Cancelled(const std::atomic<bool>* cancel) noexcept {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

} // namespace

EditorSceneComparison EditorSceneComparer::Compare(const kb::scene::SceneDocument& before, const kb::scene::SceneDocument& after,
    const std::atomic<bool>* cancel) {
    EditorSceneComparison result;
    const IndexedScene old = Index(before.worldPrefab);
    const IndexedScene now = Index(after.worldPrefab);
    std::unordered_map<std::string_view, std::uint32_t> nowByKey;
    nowByKey.reserve(now.keys.size());
    for (std::uint32_t index = 0U; index < now.keys.size(); ++index) {
        nowByKey.emplace(now.keys[index], index);
    }
    std::vector<std::uint32_t> oldPair(old.keys.size(), kNoNode);
    std::vector<bool> nowPaired(now.keys.size(), false);
    for (std::uint32_t index = 0U; index < old.keys.size(); ++index) {
        if (const auto found = nowByKey.find(old.keys[index]); found != nowByKey.end()) {
            oldPair[index] = found->second;
            nowPaired[found->second] = true;
        }
    }

    // The top of what is gone or new: an object whose parent is gone or new too is covered by it.
    for (std::uint32_t index = 0U; index < old.keys.size(); ++index) {
        const std::uint32_t parent = old.nodes[index].parentNode;
        if (oldPair[index] == kNoNode && (parent == kNoNode || oldPair[parent] != kNoNode)) {
            result.objects.push_back({ EditorSceneChangeKind::Removed, PathOf(old, index), {}, {} });
        }
    }
    for (std::uint32_t index = 0U; index < now.keys.size(); ++index) {
        const std::uint32_t parent = now.nodes[index].parentNode;
        if (!nowPaired[index] && (parent == kNoNode || nowPaired[parent])) {
            result.objects.push_back({ EditorSceneChangeKind::Added, PathOf(now, index), AddressOf(now, index), {} });
        }
    }

    // Both versions are brought to life in scenes of their own, to read their values the way the editor does.
    const bool anyPair = std::ranges::any_of(oldPair, [](std::uint32_t pair) { return pair != kNoNode; });
    if (anyPair) {
        try {
            if (Cancelled(cancel)) {
                result.error = "The comparison was cancelled.";
                return result;
            }
            kb::scene::Scene oldScene{ kb::scene::SceneMode::PrefabPrivate };
            kb::scene::Scene nowScene{ kb::scene::SceneMode::PrefabPrivate };
            const kb::scene::ScenePrefabInstance oldInstance = oldScene.Prefabs().Instantiate(before.worldPrefab);
            const kb::scene::ScenePrefabInstance nowInstance = nowScene.Prefabs().Instantiate(after.worldPrefab);
            if (oldInstance.Empty() || nowInstance.Empty()) {
                result.error = "A version of the scene could not be read.";
                return result;
            }
            const std::vector<ComponentFields> components = AllComponentFields();
            for (std::uint32_t index = 0U; index < old.keys.size(); ++index) {
                if (oldPair[index] == kNoNode) {
                    continue;
                }
                if (Cancelled(cancel)) {
                    result.objects.clear();
                    result.error = "The comparison was cancelled.";
                    return result;
                }
                std::vector<EditorSceneFieldChange> fields;
                CompareObjects(components, oldScene, oldInstance.ObjectAt(index).Entity(),
                    nowScene, nowInstance.ObjectAt(oldPair[index]).Entity(), fields);
                if (!fields.empty()) {
                    result.objects.push_back({ EditorSceneChangeKind::Changed, PathOf(now, oldPair[index]),
                        AddressOf(now, oldPair[index]), std::move(fields) });
                }
            }
        } catch (const std::exception& error) {
            result.error = error.what();
            return result;
        }
    }
    result.succeeded = true;
    return result;
}

#if defined(_WIN32)
kb::scene::SceneDocumentLoadResult EditorSceneComparer::LoadSaved(const std::filesystem::path& projectRoot, const std::string& repositoryPath) {
    kb::scene::SceneDocumentLoadResult result;
    const std::optional<std::string> saved = EditorProjectRepository::SavedContent(projectRoot, repositoryPath);
    if (!saved.has_value()) {
        result.error = "The last saved version does not have this scene.";
        return result;
    }
    // The scene codec reads files, so the saved bytes pass through one.
    static std::atomic<unsigned> counter{ 0U };
    std::error_code error;
    const std::filesystem::path file = std::filesystem::temp_directory_path(error) /
        ("kb_saved_scene_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(counter.fetch_add(1U)) + ".21kbscene");
    {
        std::ofstream output(file, std::ios::binary);
        output.write(saved->data(), static_cast<std::streamsize>(saved->size()));
    }
    result = kb::scene::SceneDocumentService::Load(file);
    std::filesystem::remove(file, error);
    return result;
}

EditorSceneComparison EditorSceneComparer::CompareWithSaved(const std::filesystem::path& projectRoot,
    const std::string& repositoryPath, const kb::scene::SceneDocument& current, const std::atomic<bool>* cancel) {
    const kb::scene::SceneDocumentLoadResult saved = LoadSaved(projectRoot, repositoryPath);
    if (!saved.succeeded) {
        return EditorSceneComparison{ .error = saved.error };
    }
    return Compare(saved.document, current, cancel);
}
#endif

kb::scene::SceneEntity EditorSceneComparer::Find(const kb::scene::Scene& scene, std::span<const std::uint32_t> address) {
    const kb::scene::SceneHierarchyQueries hierarchy = scene.Hierarchy();
    kb::scene::SceneEntity entity{};
    for (const std::uint32_t wanted : address) {
        // Objects made while the scene runs are not part of a saved version, so they do not count as siblings.
        std::uint32_t seen = 0U;
        kb::scene::SceneEntity found{};
        const std::size_t count = entity.IsValid() ? hierarchy.ChildCount(entity) : hierarchy.RootCount();
        for (std::size_t index = 0U; index < count && !found.IsValid(); ++index) {
            const kb::scene::SceneEntity candidate = entity.IsValid() ? hierarchy.ChildAt(entity, index) : hierarchy.RootAt(index);
            if (EditorHierarchyRowBuilder::IsTransient(scene, candidate)) {
                continue;
            }
            if (seen++ == wanted) {
                found = candidate;
            }
        }
        if (!found.IsValid()) {
            return {};
        }
        entity = found;
    }
    return entity;
}

} // namespace kb::editor
