#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-073: one flat notification of a scene lifecycle transition
// (name is one of "SceneLoading"/"SceneLoaded"/"SceneActivated"/
// "SceneUnloading"/"SceneUnloaded") drained from the engine's pending
// queue — see SceneLoadedContent::DrainPendingLifecycleEvents. This is the
// raw payload kb::script::ScriptRuntimeSceneSystem turns into a real
// ScriptEvent broadcast once per frame; it is deliberately NOT itself a
// subscription/event-bus mechanism (LIB-105's Events.Subscribe is not yet
// implemented) — any currently-alive, enabled behaviour receives the
// broadcast, the same untargeted-broadcast semantics every other
// DispatchEvent call in this engine already has.
struct SceneLifecycleEventRecord {
    std::string name;
    std::uint64_t sceneId = 0U;
    std::string sceneName;
};

// LIB-071: tracks scene content loaded via Scene.Load — not a second live
// kb::scene::Scene instance per loaded document (nothing in this engine
// today keeps multiple Scene objects coexisting as siblings outside
// editor-specific special cases), but a record of which root entity (and
// its whole hierarchy) came from which loaded document, inside THIS one
// Scene. additive=false replaces the scene's entire content (mirrors
// SceneDocumentService::LoadIntoScene's existing destructive-replace
// behaviour) and resets tracking to just the new load; additive=true
// instantiates alongside existing content, tracked as its own separate
// record so it can later be Unload'ed independently.
class SceneLoadedContentQueries {
public:
    explicit SceneLoadedContentQueries(const Scene& scene) noexcept;

    [[nodiscard]] std::uint64_t Find(std::string_view name) const noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    // Always 1.0 for a currently-loaded id, 0.0 for an unknown one — loads
    // are synchronous today; see SceneLoadedContentService's own comment.
    [[nodiscard]] float Progress(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t ActiveScene() const noexcept;

private:
    const Scene& scene_;
};

class SceneLoadedContent {
public:
    explicit SceneLoadedContent(Scene& scene) noexcept;

    // Returns 0 (never a valid id) on failure.
    [[nodiscard]] std::uint64_t Load(const std::filesystem::path& path, bool additive);
    [[nodiscard]] bool Unload(std::uint64_t id) noexcept;
    [[nodiscard]] std::uint64_t Find(std::string_view name) const noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] float Progress(std::uint64_t id) const noexcept;
    // Purely a tracked, queryable selection among currently-loaded records
    // today — it does NOT redirect World.Spawn/InstantiatePrefab/etc.
    // (those always target the Scene the calling ScriptRuntimeHost was
    // constructed with; rewiring every registered function's target scene
    // dynamically is a much larger, separate change, not attempted here).
    [[nodiscard]] bool SetActive(std::uint64_t id) noexcept;
    [[nodiscard]] std::uint64_t ActiveScene() const noexcept;
    // LIB-073: returns and clears every scene lifecycle notification
    // queued since the last drain. Intended to be called once per frame by
    // kb::script::ScriptRuntimeSceneSystem; a caller that never drains
    // simply leaves the queue growing — no different from any other
    // unread queue in this engine.
    [[nodiscard]] std::vector<SceneLifecycleEventRecord> DrainPendingLifecycleEvents();

private:
    Scene& scene_;
};

} // namespace kb::scene
