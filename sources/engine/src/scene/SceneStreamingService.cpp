#include "scene/SceneStreamingService.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"
#include "scene/prefab/ScenePrefabComponentApplier.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/ui/UIEntityReferences.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kb::scene {
namespace {
using Streaming = SceneStreamingState;
using Clock = std::chrono::steady_clock;

Streaming& State(Scene& scene) {
    auto& state = SceneAccess::State(scene);
    if (!state.streaming) state.streaming = std::make_unique<Streaming>();
    return *state.streaming;
}

const Streaming::Job* Find(const Scene& scene, std::uint64_t id) noexcept {
    const auto& state = SceneAccess::State(scene);
    if (!state.streaming) return nullptr;
    const auto found = std::ranges::find(state.streaming->jobs, id, &Streaming::Job::id);
    return found == state.streaming->jobs.end() ? nullptr : &*found;
}

void Event(Scene& scene, std::string name, const Streaming::Job& job) {
    SceneAccess::State(scene).pendingSceneLifecycleEvents.push_back({
        .name = std::move(name), .sceneId = job.id,
        .sceneName = job.prepared.name.empty() ? job.path.stem().string() : job.prepared.name});
}

Streaming::Prepared Prepare(std::shared_ptr<const ScenePrefab> prefab, std::string name) {
    if (!prefab || prefab->Empty() || !ScenePrefabValidator::IsValid(*prefab))
        return {.prefab = {}, .name = std::move(name), .error = "Invalid or empty streaming prefab"};
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(prefab->NodeCount());
    for (const auto& node : prefab->Nodes()) ids.insert(node.stableId);
    bool valid = true;
    for (const auto& node : prefab->Nodes()) {
        auto ui = node.components.ui;
        ForEachUIEntityReference(ui, [&ids, &valid](std::uint64_t& id) {
            valid &= id == 0U || ids.contains(id);
        });
    }
    const bool hasReferences = std::ranges::any_of(prefab->Nodes(), [](const auto& node) {
            return node.components.joint || node.components.regionPortal || node.components.lensEcho || !node.components.ui.Empty();
        });
    return {.prefab = valid ? std::move(prefab) : nullptr, .name = std::move(name),
        .error = valid ? "" : "Streaming UI reference target is missing", .hasReferences = hasReferences};
}

// Delay gameplay components and links until every target entity exists.
void DeferComponents(ScenePrefabNodeComponents& components) {
    components.joint.reset(); components.regionPortal.reset(); components.lensEcho.reset();
    components.ui = {};
    components.rigidbody.reset(); components.collider.reset(); components.characterController.reset();
    components.behaviour.reset(); components.audioSource.reset(); components.audioListener.reset();
    components.contentInstance.reset(); components.streamFocus.reset();
    components.input.reset(); components.navAgent.reset(); components.navObstacle.reset();
}

void BeginUnload(Scene& scene, Streaming::Job& job, bool cancelled) {
    if (job.status == SceneLoadStatus::Unloading) return;
    job.cancelled = cancelled;
    job.status = SceneLoadStatus::Unloading;
    if (job.root.IsValid()) job.unloadStack.push_back(job.root);
    Event(scene, "SceneUnloading", job);
}

void RemoveRecord(Scene& scene, const Streaming::Job& job) {
    auto& state = SceneAccess::State(scene);
    std::erase_if(state.loadedScenes, [&job](const auto& record) { return record.id == job.id; });
    if (state.activeLoadedSceneId == job.id) state.activeLoadedSceneId =
        state.loadedScenes.empty() ? 0U : state.loadedScenes.front().id;
}

// Destroying a large decoded document is linear work too. Keep its retirement
// owned by the scene, but never pay that cost on the Update thread.
void RetireSource(Streaming::Job& job) {
    job.preparation = std::async(std::launch::async,
        [prefab = std::move(job.prepared.prefab), source = std::move(job.source)]() mutable {
            prefab.reset();
            source.reset();
            return Streaming::Prepared{};
        });
}

std::size_t Advance(Scene& scene, Streaming::Job& job, std::size_t remaining, bool workerBusy) {
    auto& manager = scene.Assets().Manager();
    if (job.status == SceneLoadStatus::Ready && !scene.Entities().IsAlive(job.root)) {
        BeginUnload(scene, job, false);
    }
    if (job.status == SceneLoadStatus::Loading) {
        if (job.parent.IsValid() && !scene.Entities().IsAlive(job.parent)) {
            BeginUnload(scene, job, true);
            return 0U;
        }
        if (!job.preparation.valid()) {
            if (workerBusy) return 0U;
            if (job.assetId.IsValid()) {
                const auto status = manager.AsyncLoadStatus(job.assetId);
                if (status == kb::assets::AsyncAssetLoadStatus::Failed) {
                    job.error = manager.AsyncLoadError(job.assetId);
                    job.status = SceneLoadStatus::Failed;
                    Event(scene, "SceneLoadFailed", job);
                    return 0U;
                }
                if (status != kb::assets::AsyncAssetLoadStatus::Completed) return 0U;
                if (job.source) {
                    job.preparation = std::async(std::launch::async, [shared = std::move(job.source), name = job.prepared.name] {
                        return Prepare(shared, name);
                    });
                } else {
                    job.error = "Decoded streaming asset is unavailable";
                    job.status = SceneLoadStatus::Failed;
                }
            } else {
                job.preparation = std::async(std::launch::async, [path = job.path] {
                    auto loaded = SceneDocumentService::Load(path);
                    if (!loaded.succeeded) return Streaming::Prepared{.prefab = {}, .name = {}, .error = loaded.error};
                    auto shared = std::make_shared<SceneDocument>(std::move(loaded.document));
                    return Prepare(std::shared_ptr<const ScenePrefab>(shared, &shared->worldPrefab), shared->name);
                });
            }
            return 0U;
        }
        if (job.preparation.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return 0U;
        try { job.prepared = job.preparation.get(); }
        catch (const std::exception& error) { job.prepared.error = error.what(); }
        if (!job.prepared.prefab) {
            job.error = job.prepared.error;
            job.status = SceneLoadStatus::Failed;
            Event(scene, "SceneLoadFailed", job);
            return 0U;
        }
        job.status = SceneLoadStatus::Creating;
        job.root = scene.Entities().CreateEntity(SceneObjectDesc{
            .name = job.prepared.name, .parent = scene.Entities().Object(job.parent)});
        job.objects.reserve(job.prepared.prefab->NodeCount());
        if (job.prepared.hasReferences) job.references.reserve(job.prepared.prefab->NodeCount());
        return 1U;
    }
    if (job.status == SceneLoadStatus::Creating) {
        if (!scene.Entities().IsAlive(job.root) ||
            (job.parent.IsValid() && !scene.Entities().IsAlive(job.parent))) {
            BeginUnload(scene, job, true);
            return 0U;
        }
        const auto nodes = job.prepared.prefab->Nodes();
        const auto count = std::min<std::size_t>({remaining, 32U, nodes.size() - job.created});
        if (count != 0U) {
            ScenePrefab batch;
            batch.Reserve(count);
            for (std::size_t index = job.created; index < job.created + count; ++index) {
                auto node = nodes[index];
                node.parentNode = ScenePrefabNodeDesc::NoParent;
                // Nodes are created detached and attached below; they stand in for scene nodes, not prefab instances.
                node.nestedPrefabGuid.clear();
                node.visibility.mode = VisibilityMode::Hidden;
                DeferComponents(node.components);
                static_cast<void>(batch.AddNode(std::move(node)));
            }
            const auto instance = scene.Prefabs().Instantiate(batch,
                ScenePrefabInstantiationSettings{.parent = scene.Entities().Object(job.root)});
            if (instance.ObjectCount() != count) throw std::runtime_error("Streaming bulk creation failed");
            for (std::size_t index = 0U; index < count; ++index) {
                const auto object = instance.ObjectAt(static_cast<std::uint32_t>(index));
                const auto& node = nodes[job.created + index];
                job.objects.push_back(object);
                if (job.prepared.hasReferences) job.references.emplace(node.stableId, object.Entity());
                scene.Entities().SetActive(object, false);
                if (node.parentNode != ScenePrefabNodeDesc::NoParent &&
                    !object.SetParent(job.objects[node.parentNode])) throw std::runtime_error("Streaming hierarchy attachment failed");
            }
            job.created += count;
            job.progress = 0.1F + 0.45F * static_cast<float>(job.created) / static_cast<float>(nodes.size());
            return count;
        }
        const std::size_t activate = std::min<std::size_t>({remaining, 32U, nodes.size() - job.activated});
        for (std::size_t index = job.activated; index < job.activated + activate; ++index) {
            const auto object = job.objects[index];
            if (!scene.Entities().IsAlive(object)) throw std::runtime_error("Streaming entity disappeared during activation");
            auto components = nodes[index].components;
            components.ui = {};
            ScenePrefabComponentApplier::Apply(scene, object, components);
            ScenePrefabReferenceResolver::Apply(scene, nodes[index], object.Entity(), job.references);
            scene.Components().Visibility().Set(object.Entity(), nodes[index].visibility);
            scene.Tags().RegisterAssignedTags(object.Entity());
            scene.Entities().SetActive(object, true);
        }
        job.activated += activate;
        job.progress = 0.55F + 0.45F * static_cast<float>(job.activated) / static_cast<float>(nodes.size());
        if (job.activated == nodes.size()) {
            auto& state = SceneAccess::State(scene);
            state.loadedScenes.push_back({.id = job.id, .name = job.prepared.name, .path = job.path.string(), .root = job.root});
            job.status = SceneLoadStatus::Ready;
            Event(scene, "SceneLoaded", job);
            if (state.activeLoadedSceneId == 0U) {
                state.activeLoadedSceneId = job.id;
                Event(scene, "SceneActivated", job);
            }
            RetireSource(job);
            std::vector<SceneObject>{}.swap(job.objects);
            ScenePrefabReferenceResolver::EntityMap{}.swap(job.references);
        }
        return activate;
    }
    if (job.status == SceneLoadStatus::Unloading) {
        std::size_t operations = 0U;
        while (!job.unloadStack.empty() && operations < remaining) {
            const auto entity = job.unloadStack.back();
            if (!scene.Entities().IsAlive(entity)) job.unloadStack.pop_back();
            else if (const auto children = scene.Hierarchy().ChildCount(entity); children != 0U)
                job.unloadStack.push_back(scene.Hierarchy().ChildAt(entity, children - 1U));
            else {
                scene.Entities().Destroy(entity);
                job.unloadStack.pop_back();
            }
            ++operations;
        }
        // Never destroy a running future in Update: cancellation must not join I/O.
        if (job.unloadStack.empty() && (!job.preparation.valid() ||
            job.preparation.wait_for(std::chrono::seconds{0}) == std::future_status::ready)) {
            if (job.preparation.valid()) {
                try {
                    auto prepared = job.preparation.get();
                    job.prepared.prefab = std::move(prepared.prefab);
                    if (job.prepared.name.empty()) job.prepared.name = std::move(prepared.name);
                } catch (...) {}
            }
            if (job.prepared.prefab || job.source) {
                RetireSource(job);
                return operations;
            }
            RemoveRecord(scene, job);
            job.status = job.error.empty() ? (job.cancelled ? SceneLoadStatus::Cancelled : SceneLoadStatus::Unknown) : SceneLoadStatus::Failed;
            job.progress = 0.0F;
            job.source.reset(); job.prepared.prefab.reset(); job.objects.clear(); job.references.clear();
            Event(scene, job.error.empty() ? "SceneUnloaded" : "SceneLoadFailed", job);
        }
        return operations;
    }
    return 0U;
}
}

std::uint64_t SceneStreamingService::Load(Scene& scene, const std::filesystem::path& path, SceneEntity parent) {
    if (SceneAccess::State(scene).mode == SceneMode::PrefabPrivate) return 0U;
    auto& streaming = State(scene);
    if (parent.IsValid() && !scene.Entities().IsAlive(parent)) return 0U;
    auto& manager = scene.Assets().Manager();
    const auto pending = std::ranges::count_if(streaming.jobs, [&manager](const auto& job) {
        return job.status == SceneLoadStatus::Loading || job.status == SceneLoadStatus::Creating ||
            job.status == SceneLoadStatus::Unloading || job.preparation.valid() ||
            (job.status != SceneLoadStatus::Ready && job.assetId.IsValid() &&
                manager.AsyncLoadStatus(job.assetId) == kb::assets::AsyncAssetLoadStatus::Pending);
    });
    if (static_cast<std::size_t>(pending) >= streaming.settings.maxPendingLoads) return 0U;
    std::erase_if(streaming.jobs, [&manager](const auto& job) {
        const bool publishedOrStopped = !job.assetId.IsValid() ||
            (manager.AsyncLoadStatus(job.assetId) != kb::assets::AsyncAssetLoadStatus::Pending &&
                (!manager.IsLoaded(job.assetId) || manager.UnloadPolicy(job.assetId) == kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced));
        return publishedOrStopped && !job.preparation.valid() && (job.status == SceneLoadStatus::Unknown ||
            job.status == SceneLoadStatus::Cancelled || job.status == SceneLoadStatus::Failed);
    });
    Streaming::Job job;
    job.path = path; job.parent = parent;
    if (const auto* metadata = manager.Registry().FindByPath(path)) {
        if (metadata->type != "Scene" && metadata->type != "ScenePrefab") return 0U;
        job.assetId = metadata->id;
        if (!manager.RequestLoadAsync(job.assetId)) return 0U;
    } else if (manager.IsRuntimePackMounted() || kb::assets::AssetPathUtilities::IsMountedVirtualPath(manager.Mounts(), path)) return 0U;
    job.id = SceneAccess::State(scene).nextLoadedSceneId++;
    Event(scene, "SceneLoading", job);
    streaming.jobs.push_back(std::move(job));
    return streaming.jobs.back().id;
}

bool SceneStreamingService::Unload(Scene& scene, std::uint64_t id) {
    auto& streaming = State(scene);
    auto found = std::ranges::find(streaming.jobs, id, &Streaming::Job::id);
    if (found == streaming.jobs.end()) {
        const auto& records = SceneAccess::State(scene).loadedScenes;
        const auto record = std::ranges::find(records, id, &SceneState::LoadedSceneRecord::id);
        if (record == records.end()) return false;
        Streaming::Job job;
        job.id = id; job.root = record->root; job.prepared.name = record->name; job.status = SceneLoadStatus::Ready;
        streaming.jobs.push_back(std::move(job));
        found = streaming.jobs.end() - 1;
    }
    if (found->status == SceneLoadStatus::Failed || found->status == SceneLoadStatus::Cancelled || found->status == SceneLoadStatus::Unknown) return false;
    BeginUnload(scene, *found, found->status != SceneLoadStatus::Ready);
    return true;
}

void SceneStreamingService::Pump(Scene& scene) {
    auto& state = SceneAccess::State(scene);
    if (!state.streaming) return;
    auto& streaming = *state.streaming;
    const auto start = Clock::now();
    streaming.stats = {};
    scene.Assets().Manager().PumpAsyncLoads();
    // Acquire strong ownership before switching the decoded cache to weak
    // retention. Several cells can request the same source concurrently.
    auto& manager = scene.Assets().Manager();
    for (auto& job : streaming.jobs) {
        if (job.status != SceneLoadStatus::Loading && job.status != SceneLoadStatus::Creating &&
            job.status != SceneLoadStatus::Unloading && job.preparation.valid() &&
            job.preparation.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try { static_cast<void>(job.preparation.get()); } catch (...) {}
        }
        if (job.status != SceneLoadStatus::Loading || !job.assetId.IsValid() || job.source || job.preparation.valid()) continue;
        const auto document = manager.AcquireLoaded<SceneDocument>(job.assetId);
        const auto prefab = manager.AcquireLoaded<ScenePrefab>(job.assetId);
        if (document.IsLoaded()) {
            const auto shared = document.Shared();
            job.source = std::shared_ptr<const ScenePrefab>(shared, &shared->worldPrefab);
            job.prepared.name = shared->name;
        } else if (prefab.IsLoaded()) {
            job.source = prefab.Shared();
            job.prepared.name = job.path.stem().string();
        }
        if (job.source) static_cast<void>(manager.SetUnloadPolicy(job.assetId, kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced));
    }
    for (auto& job : streaming.jobs) {
        if (job.status != SceneLoadStatus::Loading && job.status != SceneLoadStatus::Creating &&
            job.assetId.IsValid() && manager.ReferenceCount(job.assetId) == 0U) {
            // A cancelled asset request can publish after cancellation. Retain
            // it before weakening the cache so its document also dies off-thread.
            {
                const auto document = manager.AcquireLoaded<SceneDocument>(job.assetId);
                const auto prefab = manager.AcquireLoaded<ScenePrefab>(job.assetId);
                if (document.IsLoaded()) {
                    const auto shared = document.Shared();
                    job.source = std::shared_ptr<const ScenePrefab>(shared, &shared->worldPrefab);
                } else if (prefab.IsLoaded()) job.source = prefab.Shared();
                static_cast<void>(manager.SetUnloadPolicy(job.assetId, kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced));
            }
            if (job.source && !job.preparation.valid()) RetireSource(job);
        }
    }
    const auto expired = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - start).count() >= streaming.settings.maxMillisecondsPerFrame; };
    const std::size_t size = streaming.jobs.size();
    std::size_t idle = 0U;
    while (size != 0U && idle < size && streaming.stats.operations < streaming.settings.maxOperationsPerFrame && !expired()) {
        const std::size_t index = streaming.nextJob++ % size;
        auto& job = streaming.jobs[index];
        const bool busy = std::ranges::any_of(streaming.jobs, [](const auto& candidate) { return candidate.preparation.valid(); });
        std::size_t operations = 0U;
        try {
            operations = Advance(scene, job, streaming.settings.maxOperationsPerFrame - streaming.stats.operations, busy);
        } catch (const std::exception& error) {
            job.error = error.what();
            BeginUnload(scene, job, true);
        }
        streaming.stats.operations += operations;
        idle = operations == 0U ? idle + 1U : 0U;
    }
    streaming.stats.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
SceneLoadStatus SceneStreamingService::Status(const Scene& scene, std::uint64_t id) noexcept {
    const auto* job = Find(scene, id);
    if (job) return job->status;
    const auto& records = SceneAccess::State(scene).loadedScenes;
    return std::ranges::any_of(records, [id](const auto& record) { return record.id == id; }) ? SceneLoadStatus::Ready : SceneLoadStatus::Unknown;
}
void SceneStreamingService::CancelPending(Scene& scene) {
    auto& state = SceneAccess::State(scene);
    if (!state.streaming) return;
    for (auto& job : state.streaming->jobs) {
        if (job.status == SceneLoadStatus::Loading || job.status == SceneLoadStatus::Creating)
            BeginUnload(scene, job, true);
    }
}
float SceneStreamingService::Progress(const Scene& scene, std::uint64_t id) noexcept {
    const auto* job = Find(scene, id);
    return job ? job->progress : (Status(scene, id) == SceneLoadStatus::Ready ? 1.0F : 0.0F);
}
std::string SceneStreamingService::Error(const Scene& scene, std::uint64_t id) {
    const auto* job = Find(scene, id);
    return job ? job->error : std::string{};
}
void SceneStreamingService::Configure(Scene& scene, SceneStreamingSettings settings) {
    if (settings.maxPendingLoads == 0U || settings.maxOperationsPerFrame == 0U ||
        !std::isfinite(settings.maxMillisecondsPerFrame) || settings.maxMillisecondsPerFrame <= 0.0F)
        throw std::invalid_argument("Streaming budgets must be finite and positive");
    State(scene).settings = settings;
}
SceneStreamingStats SceneStreamingService::Stats(const Scene& scene) noexcept {
    const auto& state = SceneAccess::State(scene);
    return state.streaming ? state.streaming->stats : SceneStreamingStats{};
}
}
