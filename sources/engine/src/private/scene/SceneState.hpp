#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/core/ReadSnapshotQueue.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"
#include "engine/input/InputLocalUser.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/scene/BehaviourVariableOverride.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/save/SaveGame.hpp"
#include "engine/scene/SceneMode.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/TimelineAsset.hpp"
#include "engine/scene/UIAssets.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorage.hpp"
#include "scene/history/SceneHistoryStack.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ecs_query_t;

namespace kb::audio {

class IAudioPlaybackBackend;

} // namespace kb::audio

namespace kb::input {

class IInputHapticsBackend;

} // namespace kb::input

namespace kb::scene {

class IPhysicsBackend;

struct AnimatorRuntimeState {
    struct Motion {
        kb::assets::AssetHandle<AnimationClip> clip;
        std::uint64_t clipLoadGeneration = 0U;
        std::vector<std::size_t> targetIndices;
        std::vector<std::uint32_t> boneIndices;
        std::uint32_t rootMotionBoneIndex =
            std::numeric_limits<std::uint32_t>::max();
    };
    std::vector<Motion> motions;
    std::size_t blendParameterIndex = std::numeric_limits<std::size_t>::max();
};

struct AnimatorRuntimeBinding {
    SceneEntity target{};
    LocalTransform bindTransform{};
    LocalTransform output{};
    LocalTransform fromPose{};
    LocalTransform toPose{};
    bool outputTouched = false;
    bool fromTouched = false;
    bool toTouched = false;
};

struct AnimatorRuntimeLayer {
    std::vector<AnimatorRuntimeState> states;
    std::size_t currentState = 0U;
    std::size_t previousState = 0U;
    double currentTimeSeconds = 0.0;
    double previousTimeSeconds = 0.0;
    double transitionElapsedSeconds = 0.0;
    double transitionDurationSeconds = 0.0;
    bool transitioning = false;
};

struct AnimatorRuntimeConstraint {
    std::size_t definitionIndex = 0U;
    SceneEntity constrained{};
    SceneEntity mid{};
    SceneEntity tip{};
    std::uint32_t constrainedBoneIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t midBoneIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t tipBoneIndex =
        std::numeric_limits<std::uint32_t>::max();
};

struct AnimatorInstanceSkeleton {
    struct PoseSoa {
        std::vector<Vec3> positions;
        std::vector<Quat> rotations;
        std::vector<Vec3> scales;
    };
    struct PoseBuffer {
        PoseSoa local;
        PoseSoa component;
        std::vector<kb::math::Mat4> skinMatrices;
    };

    kb::assets::AssetHandle<SkeletonAsset> asset;
    std::uint64_t loadGeneration = 0U;
    std::uint64_t compatibilitySignature = 0U;
    std::vector<SkeletonBoneId> boneIds;
    std::unordered_map<SkeletonBoneId, std::uint32_t> boneIndices;
    std::array<PoseBuffer, 2U> poses;
    std::array<PoseSoa, 2U> stateScratch;
    std::array<std::vector<std::uint8_t>, 2U> stateTouched;
    std::array<std::vector<std::uint8_t>, 2U> motionTouched;
    std::uint32_t currentPose = 0U;
    std::uint64_t evaluationCount = 0U;
    std::uint64_t hierarchySolveCount = 0U;
};

struct AnimatorInstance {
    SceneEntity entity{};
    kb::assets::AssetHandle<AnimatorController> controller;
    std::uint64_t controllerLoadGeneration = 0U;
    // Monotonic identity of this derived attachment. Physics queues observe it
    // to discard deltas produced before any controller/clip/hierarchy rebind.
    std::uint64_t runtimeBindingGeneration = 0U;
    std::uint64_t observedHierarchyTopologyVersion = 0U;
    std::vector<AnimatorRuntimeLayer> layers;
    std::vector<AnimatorRuntimeBinding> bindings;
    std::optional<AnimatorInstanceSkeleton> skeleton;
    std::vector<AnimatorParameterValue> parameters;
    std::vector<AnimatorRuntimeConstraint> rigConstraints;
    std::map<std::string, AnimatorIkTarget, std::less<>> ikTargets;
    Vec3 extractedRootMotionTranslation{};
    Quat extractedRootMotionRotation{};
    bool hasExtractedRootMotion = false;
    // Live speed is script-mutable. This cache only detects a later authored
    // component edit; it is never exposed or serialized as independent state.
    float speed = 1.0F;
    float lastAppliedComponentSpeed = 1.0F;
};

using AnimatorRuntimeRecord = AnimatorInstance;

struct TimelineRuntimeBinding {
    SceneEntity target{};
    bool explicitlyBound = false;
};

struct TimelineRuntimeRecord {
    std::uint64_t id = 0U;
    SceneEntity owner{};
    kb::assets::AssetHandle<TimelineAsset> asset;
    std::uint64_t assetLoadGeneration = 0U;
    std::uint64_t observedHierarchyTopologyVersion = 0U;
    std::map<std::string, TimelineRuntimeBinding, std::less<>> bindings;
    float timeSeconds = 0.0F;
    bool playing = false;
};

struct UIDocumentRuntimeRecord {
    SceneEntity entity{};
    kb::assets::AssetHandle<UIDocument> document;
    kb::assets::AssetHandle<UIStyleAsset> style;
    std::uint64_t documentLoadGeneration = 0U;
    std::uint64_t styleLoadGeneration = 0U;
    UIElementId root = 0U;
    std::map<UIElementId, UIDocumentElement> elements;
    UIElementId nextRuntimeElementId = 1U;
    struct BindingState {
        UIBindingDeclaration declaration;
        std::optional<UIBindingValue> lastSourceValue;
        std::optional<UIBindingValue> lastControlValue;
        bool initialized = false;
    };
    std::vector<BindingState> bindings;
    UIElementId focusedElement = 0U;
    struct VirtualListState {
        std::uint32_t viewportItems = 0U;
        std::uint32_t overscan = 0U;
        std::uint32_t firstVisibleIndex = 0U;
        std::uint32_t activeItemCount = 0U;
        std::vector<UIVirtualListItem> pool;
    };
    std::map<UIElementId, VirtualListState> virtualLists;
};

// Derived execution state for ContentInstanceComponent. It deliberately keeps
// no authorable fields beyond a cache key used to recognize a component edit.
// The ECS component remains the sole source of placement, source and lifetime
// policy; root/loadedSceneId are disposable runtime handles.
struct ContentInstanceRuntimeRecord {
    SceneEntity owner{};
    std::uint64_t assetId = 0U;
    ContentInstanceKind kind = ContentInstanceKind::Prefab;
    ContentInstanceLifetime lifetime = ContentInstanceLifetime::Owner;
    SceneEntity root{};
    std::uint64_t loadedSceneId = 0U;
    kb::assets::AssetHandle<ScenePrefab> prefab;
};

enum class UIRuntimeCommandKind : std::uint8_t {
    Create,
    Destroy,
    SetVisible,
    SetControl,
    ConfigureVirtualList,
    ScrollVirtualList,
};

struct UIRuntimeCommand {
    UIRuntimeCommandKind kind = UIRuntimeCommandKind::Create;
    SceneEntity entity{};
    UIElementId elementId = 0U;
    UIRuntimeElementDesc create{};
    bool visible = true;
    UIControlState control;
    std::uint32_t viewportItems = 0U;
    std::uint32_t overscan = 0U;
    std::uint32_t firstVisibleIndex = 0U;
};

struct PendingUIRuntimeEvent {
    SceneEntity entity{};
    UIRuntimeEvent event{};
};

struct SceneTransformValueCacheEntry {
    SceneEntity entity;
    TransformComponent transform{};
    std::uint64_t cacheVersion = 0U;
    bool valid = false;
    bool dirty = false;
};

struct SceneTransformApplyChunkStats {
    std::size_t updated = 0U;
    std::size_t rootFastPath = 0U;
    std::size_t translatedParentFastPath = 0U;
    std::size_t unrotatedParentFastPath = 0U;
    std::size_t unitScaleParentFastPath = 0U;
    std::size_t uniformScaleParentFastPath = 0U;
    std::size_t staticLocalRotationFastPath = 0U;
};

class SceneState {
public:
    SceneState();
    explicit SceneState(kb::ecs::WorldConfig worldConfig);
    ~SceneState();

    SceneState(const SceneState&) = delete;
    SceneState& operator=(const SceneState&) = delete;
    SceneState(SceneState&&) = delete;
    SceneState& operator=(SceneState&&) = delete;

    kb::ecs::World world;
    SceneComponentRegistry components;
    SceneComponentStorage componentStorage;
    // Single source of truth for authorable/runtime tag definitions. Entity
    // TagsComponent instances only reference values from this list.
    std::vector<std::string> tagDefinitions{ "Player", "Enemy", "Monster", "AI", "NPC", "Collision" };
    ScenePrefabRegistry prefabs;
    ScenePrefabInstanceRegistry prefabInstances;
    ScenePrefabInstantiationStats lastPrefabInstantiationStats;
    bool suppressPrefabDirtyTracking = false;
    SceneMode mode = SceneMode::Runtime;
    kb::assets::AssetManager assets;
    kb::assets::AssetHandle<kb::localization::LocalizationCatalog> localizationCatalog;
    std::uint64_t localizationCatalogGeneration = 0U;
    std::string localizationLanguage;
    std::map<std::uint64_t, AnimatorInstance> animators;
    std::vector<AnimationEventRecord> pendingAnimationEvents;
    std::map<std::uint64_t, TimelineRuntimeRecord> timelines;
    std::map<std::uint64_t, UIDocumentRuntimeRecord> uiDocuments;
    std::map<std::uint64_t, ContentInstanceRuntimeRecord> contentInstances;
    // LIB-174: sole mutable UI write boundary. It is FIFO and drained by the
    // UI scene system, never by callers, so runtime tree iteration cannot be
    // invalidated by script/API mutations mid-frame.
    std::vector<UIRuntimeCommand> pendingUICommands;
    // LIB-176: input producers never invoke scripts directly. They append
    // here; ScriptRuntimeSceneSystem drains this FIFO after UI commands have
    // reached their frame boundary and emits through ScriptEventBus.
    std::vector<PendingUIRuntimeEvent> pendingUIEvents;
    // Exactly one retained document receives physical UI input. Its focus
    // remains document-local so independent documents can expose their own
    // accessibility state without receiving each other's interactions.
    SceneEntity activeUIDocument{};
    bool uiPointerWasDown = false;
    bool uiSubmitWasDown = false;
    bool uiNextWasDown = false;
    bool uiPreviousWasDown = false;
    std::vector<TimelineMarkerEvent> pendingTimelineMarkerEvents;
    std::uint64_t nextTimelineInstanceId = 1U;
    kb::input::InputSubsystem inputSubsystem;
    // LIB-115: independent input state for local users other than the primary
    // (split-screen / shared-keyboard local co-op). Keyed by LocalUserId::value;
    // lazily created on first access (see Scene::Input(LocalUserId)). unordered_map
    // never moves/invalidates existing values on insert, so held InputSubsystem&
    // references stay valid across later insertions.
    std::unordered_map<std::uint32_t, kb::input::InputSubsystem> secondaryInputSubsystems;
    SceneHistoryStack undoHistory;
    SceneHistoryStack redoHistory;
    kb::ecs::SystemScheduler systemScheduler;
    SceneSystemScheduler sceneSystemScheduler;
    SceneRuntimeFixedStepSettings fixedStepSettings;
    // The delta exposed by Time.FixedDelta. In an installed production
    // ScriptRuntimeSceneSystem this mirrors fixedStepSettings exactly: script
    // FixedTick and physics consume one authoritative accumulator. It remains
    // separate storage only for direct/manual ExecuteFrame callers, which are
    // outside the scene scheduler.
    float scriptFixedDeltaSeconds = 1.0F / 60.0F;
    SceneTransformPropagationBudget transformPropagationBudget;
    // True once a fixed-step system (e.g. physics) has been added. Scenes without
    // one skip the fixed-step substep loop and its per-substep interpolation
    // sampling, which otherwise rebuilds a map of every transform each substep.
    bool requiresFixedStep = false;
    float fixedStepAccumulatorSeconds = 0.0F;
    float fixedInterpolationAlpha = 0.0F;
    std::size_t lastFixedStepCount = 0U;
    // LIB-065: monotonic counters, never reset (unlike lastFixedStepCount,
    // which is a per-frame count reset at the top of every Update()).
    // frameIndex counts Update() calls; fixedStepIndex counts individual
    // fixed-step substeps across the scene's whole lifetime.
    std::uint64_t frameIndex = 0U;
    std::uint64_t fixedStepIndex = 0U;
    // LIB-093: total simulated seconds across the scene's whole lifetime —
    // accumulates the RAW (unclamped) deltaSeconds passed to every Update()
    // call, mirroring frameIndex's own "counts every call, unconditionally
    // including PrefabPrivate scenes" convention (SceneRuntimeService::
    // Update, SceneRuntime.cpp) rather than only the (possibly clamped)
    // delta the fixed-step accumulator uses — Elapsed answers "how much
    // wall-clock-equivalent time has this scene been simulating," not
    // "how much fixed-step time has been consumed."
    double elapsedSeconds = 0.0;
    // LIB-065: true by default (a standalone/headless run has no "edit
    // mode" to distinguish from) — an editor sets this false while the
    // scene is being edited rather than simulated, via SetPlaying.
    bool isPlaying = true;
    // LIB-094: script-visible time multiplier, applied ONLY at the
    // Time.Delta boundary (ScriptTimeApi.cpp), never to the raw deltaSeconds
    // threaded through SceneRuntimeService::Update/ScriptRuntimeSceneSystem
    // — physics/ECS/elapsedSeconds/frameIndex are all deliberately
    // untouched by this field, so slowing or pausing script-visible time
    // never distorts the engine's own notion of simulated time. Always
    // >= 0 (SetTimeScale rejects negative values rather than clamping them
    // away silently). Default 1.0 = unscaled, matching today's behavior.
    float timeScale = 1.0F;
    std::uint64_t runtimeSnapshotRevision = 0U;
    kb::core::ReadSnapshotPublisher<SceneRuntimeReadSnapshot> runtimeSnapshots;
    kb::core::CommandQueue<SceneRuntimeCommand> runtimeCommands;
    // LIB-095: one scheduled Timer.Once/Timer.Repeat entry. `owner` invalid
    // (default SceneEntity{}) means "no owner" — TimerFired broadcasts to
    // every enabled behaviour (LIB-073's untargeted-broadcast convention)
    // instead of a targeted dispatch. `id` is assigned from nextTimerId,
    // never reused within a scene's lifetime (same convention as
    // nextLoadedSceneId above), so a stale id can never collide with a
    // later, unrelated timer. `intervalSeconds` is only meaningful when
    // `repeating` is true; a one-shot timer is removed from `timers` the
    // moment it fires, a repeating one has `remainingSeconds` reset to
    // `intervalSeconds` and stays alive. `paused` freezes `remainingSeconds`
    // exactly (SceneTimerService::Advance skips decrementing it) — mirrors
    // ScriptRuntimeSceneSystem's own FixedTick-during-scene-pause rule
    // (LIB-094): no debt accumulates while paused, no catch-up burst on
    // resume.
    struct TimerRecord {
        std::uint64_t id = 0U;
        SceneEntity owner{};
        float remainingSeconds = 0.0F;
        float intervalSeconds = 0.0F;
        bool repeating = false;
        bool paused = false;
        // LIB-101: creation-site diagnostics — the entity that CALLED
        // Timer.Once/Repeat (ScriptFunctionCallContext::caller for a script
        // call, invalid for a native-C++-only SceneTimers::Once/Repeat
        // call that didn't supply one), distinct from `owner` above (who
        // RECEIVES TimerFired, not who wrote the call that created the
        // timer) — lets a hung/leaked/misbehaving timer be traced back to
        // whatever created it, e.g. via Timer.Creator(handle).
        SceneEntity creator{};
    };
    std::vector<TimerRecord> timers;
    std::uint64_t nextTimerId = 1U;
    // LIB-097: one live Task started through kb::scene::SceneTasks::Start —
    // `poll` is a NATIVE C++-only callable (never script-authored, see
    // SceneTasks.hpp's class doc comment for the full Coroutine/Task model
    // decision), called once per frame by SceneTaskService::Advance with
    // the same scaled/pause-aware deltaSeconds Timer uses, until it
    // reports anything other than Running — at that point the record is
    // removed and a TaskCompleted/TaskFailed event is dispatched. `owner`
    // follows the exact same auto-cancel-on-death convention as
    // TimerRecord::owner above.
    struct TaskRecord {
        std::uint64_t id = 0U;
        SceneEntity owner{};
        std::function<TaskPollResult(float)> poll;
        // LIB-098: false = Frame-domain (SceneTasks::Start, polled by
        // Advance with elapsed seconds); true = FixedTick-domain
        // (SceneTasks::StartFixedStep, polled by AdvanceFixedSteps with a
        // step count) — see SceneTasks.hpp's class doc comment for why
        // these need two separate Advance call sites.
        bool fixedStepDomain = false;
        // LIB-101: creation-site diagnostics — see TimerRecord::creator's
        // own doc comment above for the full reasoning. A Task is
        // native-C++-only to create (SceneTasks.hpp), so this is whatever
        // the native caller explicitly passed, not derived from a
        // ScriptFunctionCallContext (none exists at a Task's creation
        // site).
        SceneEntity creator{};
    };
    std::vector<TaskRecord> tasks;
    std::uint64_t nextTaskId = 1U;
    // LIB-139: one live runtime MaterialInstance created through
    // kb::scene::SceneMaterialInstances::Create - a scene-side indirection
    // recording only which parent material asset it stands in for (LIB-140's
    // scope adds per-parameter overrides on top of this record; LIB-139
    // deliberately does not carry any parameter data yet). `id` is monotonic
    // and never reused within a scene's lifetime, mirroring TimerRecord::id's
    // own convention exactly (see SceneTimerService.cpp's rationale) - a
    // stale id can never collide with a live one, so no generation counter
    // is needed for "explicit lifetime" (Release'd once, gone forever).
    struct MaterialInstanceRecord {
        std::uint64_t id = 0U;
        std::uint64_t parentMaterialAssetId = 0U;
        // LIB-140: named overrides admitted through the renderer-owned schema bridge.
        std::vector<MaterialParameterOverride> parameterOverrides;
    };
    std::vector<MaterialInstanceRecord> materialInstances;
    std::uint64_t nextMaterialInstanceId = 1U;
    std::shared_ptr<const MaterialParameterSchemaValidator> materialParameterSchemaValidator;
    struct FixedTransformSample {
        TransformComponent previous;
        TransformComponent current;
    };
    std::unordered_map<SceneEntity::IdType, FixedTransformSample> fixedTransformSamples;
    std::unordered_map<SceneEntity::IdType, TransformComponent> fixedTransformStepStart;
    std::vector<std::string> denseEntityNames;
    std::unordered_map<SceneEntity::IdType, std::string> entityNames;
    // Per-instance overrides of behaviours' exposed ("@expose") script variables,
    // authored in the Inspector — a side-table because BehaviourComponent is a
    // fixed POD in the archetype store and cannot hold variable-length data.
    // Delta-over-default: only variables whose value differs from the script's
    // declared default are stored (same "present means non-default authored
    // data" shape as entityNames). Seeded into the script runtime before each
    // behaviour's Created lifecycle.
    std::unordered_map<SceneEntity::IdType, std::vector<BehaviourVariableOverride>> behaviourVariableOverrides;
    // LIB-068: entities are active by default (absent from this set); an
    // entity is only ever added here by an explicit SetActive(false) —
    // same "presence means non-default" shape already used by
    // hierarchyParents/hierarchyChildren for the (much more common)
    // default-empty case, so a scene with no deactivated entities pays no
    // per-entity storage cost.
    std::unordered_set<SceneEntity::IdType> inactiveEntities;
    // LIB-072: the persistent/gameplay boundary — a ROOT entity present here
    // survives ClearSceneRoots (SceneDocumentService::LoadIntoScene's
    // non-additive-Scene.Load wipe), along with its whole hierarchy (Destroy
    // cascades to children, so skipping a persistent root preserves its
    // entire subtree without needing separate per-child tracking). Same
    // "presence means non-default" flat-set shape as inactiveEntities.
    // Marking a NON-root entity persistent has no protective effect on its
    // own — ClearSceneRoots only ever evaluates roots, so a persistent
    // child still cascade-destroys if its non-persistent parent's root is
    // cleared (mirrors Unity's DontDestroyOnLoad, which likewise only
    // affects root-level GameObjects); this is an intentional, documented
    // scope limit, not an oversight.
    std::unordered_set<SceneEntity::IdType> persistentEntities;
    // LIB-071: content loaded via Scene.Load (additive or not). Each
    // record names ONE loaded document's own root entity so it can later
    // be selectively Scene.Unload'ed without touching content loaded from
    // a different document. `id` is assigned by SceneLoadedContentService,
    // never reused within a scene's lifetime (nextLoadedSceneId only ever
    // increments), so a stale handle from an already-unloaded record can
    // never collide with a later one.
    struct LoadedSceneRecord {
        std::uint64_t id = 0U;
        std::string name;
        std::string path;
        SceneEntity root{};
    };
    std::vector<LoadedSceneRecord> loadedScenes;
    std::uint64_t nextLoadedSceneId = 1U;
    std::uint64_t activeLoadedSceneId = 0U;
    // LIB-073: scene lifecycle notifications (SceneLoading/SceneLoaded/
    // SceneActivated/SceneUnloading/SceneUnloaded) queued by
    // SceneLoadedContentService — an "unambiguous command deferred to a
    // queue" (Engine21kbLibrary.md's own invariant #3), not a second
    // source of truth: kb::scene never interprets these itself. Drained
    // once per frame by kb::script::ScriptRuntimeSceneSystem via the
    // public SceneLoadedContent::DrainPendingLifecycleEvents() facade and
    // turned into real ScriptEvent broadcasts.
    struct PendingSceneLifecycleEvent {
        std::string name;
        std::uint64_t sceneId = 0U;
        std::string sceneName;
    };
    std::vector<PendingSceneLifecycleEvent> pendingSceneLifecycleEvents;
    // LIB-067: entities queued by World.Destroy(deferred=true), destroyed at
    // the next frame playback point (drained once per frame by
    // ScriptRuntimeSceneSystem via SceneEntities::DrainDeferredDestroys).
    // Deferring keeps a script from destroying an entity mid-behaviour-
    // iteration; the full SceneEntity (id+generation) is stored so each id is
    // re-checked for liveness at drain — a racing immediate destroy, a stale
    // generation, or a duplicate deferred request is a harmless no-op.
    std::vector<SceneEntity> pendingDeferredDestroys;
    std::unordered_map<SceneEntity::IdType, std::uint64_t> hierarchyOrder;
    std::vector<std::uint64_t> denseHierarchyOrder;
    std::vector<SceneEntity> hierarchyRoots;
    std::unordered_map<SceneEntity::IdType, SceneEntity> hierarchyParents;
    std::unordered_map<SceneEntity::IdType, std::vector<SceneEntity>> hierarchyChildren;
    std::vector<SceneEntity> denseHierarchyParents;
    std::vector<std::vector<SceneEntity>> denseHierarchyChildren;
    std::vector<std::size_t> prefabHierarchyChildrenPerNodeScratch;
    std::vector<std::vector<SceneEntity>> transformTopologicalBatches;
    std::uint64_t hierarchyTopologyVersion = 1;
    std::uint64_t nextAnimatorRuntimeBindingGeneration = 1U;
    std::uint64_t transformTopologicalBatchesVersion = 0;
    std::uint64_t transformTopologicalBatchBuildCount = 0;
    std::uint64_t transformPropagationCursorVersion = 0;
    std::size_t transformPropagationCursorLevel = 0U;
    std::size_t transformPropagationCursorOffset = 0U;
    std::unordered_map<SceneEntity::IdType, bool> transformDirtyScratch;
    std::unordered_map<SceneEntity::IdType, TransformComponent> transformWorldScratch;
    std::vector<TransformComponent> denseTransformWorldScratch;
    std::vector<std::uint32_t> denseTransformWorldScratchValid;
    std::vector<std::uint8_t> denseTransformDirtyScratch;
    std::uint32_t denseTransformWorldScratchEpoch = 1U;
    std::uint64_t transformValueCacheBuildVersion = 1U;
    std::vector<SceneTransformValueCacheEntry> transformValueDenseScratch;
    std::unordered_map<SceneEntity::IdType, SceneTransformValueCacheEntry> transformValueSparseScratch;
    std::vector<SceneEntity> transformValueCacheLoadEntitiesScratch;
    std::vector<SceneEntity> transformValueCacheLoadDenseMarkedEntities;
    std::vector<std::uint32_t> transformValueCacheLoadDenseMarkEpochs;
    std::unordered_map<SceneEntity::IdType, std::uint32_t> transformValueCacheLoadSparseMarkEpochs;
    std::uint32_t transformValueCacheLoadMarkEpoch = 1U;
    std::vector<SceneTransformBatchEntry> transformHierarchyEntriesScratch;
    std::vector<SceneEntity> transformHierarchyUpdatedEntitiesScratch;
    std::vector<TransformComponent> transformHierarchyUpdatedTransformsScratch;
    std::vector<TransformComponent> transformHierarchyFlushComponentsScratch;
    std::vector<SceneTransformApplyChunkStats> transformHierarchyApplyChunkStatsScratch;
    std::vector<kb::ecs::NativeComponentDirtyRange> transformNativeDirtyRangesScratch;
    std::vector<SceneEntity> transformDirtyFrontierEntities;
    std::vector<SceneEntity> transformDirtyFrontierDenseMarkedEntities;
    std::vector<std::uint32_t> transformDirtyFrontierDenseMarkEpochs;
    std::unordered_map<SceneEntity::IdType, std::uint32_t> transformDirtyFrontierSparseMarkEpochs;
    std::uint32_t transformDirtyFrontierMarkEpoch = 1U;
    std::vector<SceneEntity> transformDirtyFrontierLevelScratch;
    std::vector<SceneEntity> transformDirtyFrontierNextScratch;
    std::vector<std::uint8_t> renderProxyDenseComponentMasks;
    std::unordered_map<SceneEntity::IdType, std::uint8_t> renderProxySparseComponentMasks;
    std::vector<SceneEntity> transformRenderProxyUpdateEntities;
    std::vector<WorldTransformAffine3x4> transformRenderProxyWorldAffine3x4;
    std::vector<std::size_t> transformRenderProxyMeshRendererIndices;
    std::vector<std::size_t> transformRenderProxyVisibleMeshRendererIndices;
    std::vector<std::size_t> transformRenderProxyCameraIndices;
    std::vector<std::size_t> transformRenderProxyLightIndices;
    std::vector<SceneEntity> meshRendererRenderProxyUpdateEntities;
    std::vector<std::size_t> transformRenderProxyIdentityAffineChunkCountsScratch;
    std::size_t lastTransformRenderProxyIdentityAffineFastPathCount = 0U;
    std::unique_ptr<kb::ecs::WorkerPool> transformWorkerPool;
    std::size_t lastTransformHierarchyInspectedCount = 0U;
    std::size_t lastTransformHierarchyUpdatedCount = 0U;
    std::size_t lastTransformHierarchyRootFastPathCount = 0U;
    std::size_t lastTransformHierarchyTranslatedParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUnrotatedParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUnitScaleParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUniformScaleParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyStaticLocalRotationFastPathCount = 0U;
    std::size_t lastTransformHierarchySparseFlushCount = 0U;
    std::size_t lastTransformHierarchyDirtyListFlushCount = 0U;
    std::size_t lastTransformHierarchyDirtyListFlushEntityCount = 0U;
    std::size_t lastTransformHierarchyBatchFlushCount = 0U;
    std::size_t lastTransformHierarchyFlushedEntityCount = 0U;
    std::size_t lastTransformHierarchyDirtyFrontierCount = 0U;
    std::size_t lastTransformHierarchyParallelBatchCount = 0U;
    std::size_t lastTransformHierarchyParallelChunkCount = 0U;
    std::size_t lastTransformHierarchyParallelEntityCount = 0U;
    std::size_t lastTransformHierarchyWorkerCount = 1U;
    std::size_t lastTransformHierarchyParallelFlushCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushChunkCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushEntityCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushWorkerCount = 1U;
    std::uint64_t lastTransformHierarchyCacheBuildNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyEntryBuildNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyKernelApplyNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFrontierAppendNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyPropagateNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFlushWriteNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyBackendMarkNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyUpdateNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFlushNanoseconds = 0U;
    bool lastTransformHierarchyBudgetExhausted = false;
    mutable ecs_query_t* cameraIterationQuery = nullptr;
    mutable ecs_query_t* lightIterationQuery = nullptr;
    mutable ecs_query_t* meshRendererIterationQuery = nullptr;
    mutable ecs_query_t* visibleMeshRendererIterationQuery = nullptr;
    mutable ecs_query_t* physicsBodyIterationQuery = nullptr;
    std::uint64_t nextHierarchyOrder = 1;
    kb::audio::IAudioPlaybackBackend* audioPlaybackBackend = nullptr;
    // LIB-153: host-registered haptics actuator (mirror of audioPlaybackBackend - the
    // editor's Win32/XInput layer registers it for Play Mode; nullptr = every haptics
    // query honestly reports unsupported).
    kb::input::IInputHapticsBackend* inputHapticsBackend = nullptr;
    std::unordered_map<std::uint32_t, std::uint32_t> hapticsGamepadByLocalUser;
    std::unordered_map<std::uint64_t, std::unordered_map<std::uint64_t, kb::assets::AssetOpaqueHandle>> scriptOwnedAssets;
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> scriptPendingAssets;
    struct ScriptPrefabParameter {
        std::uint32_t nodeIndex = 0U;
        std::string component;
        std::string property;
        kb::script::ScriptValue value;
    };
    struct ScriptPrefabParameterSet {
        SceneEntity owner;
        std::vector<ScriptPrefabParameter> parameters;
    };
    std::uint64_t nextScriptPrefabParameterSetId = 1U;
    std::unordered_map<std::uint64_t, ScriptPrefabParameterSet> scriptPrefabParameterSets;
    IPhysicsBackend* physicsBackend = nullptr;
    bool basicLightingEnabled = false;
    // LIB-142: scene-global active PostProcessProfile asset id (0 = none) - the ONLY
    // asset-based, serializable post-process parameter set a scene can be assigned, mirroring
    // basicLightingEnabled's own scene-global-toggle shape rather than a per-entity component
    // (a spatial, per-camera/per-volume post-process system is explicitly out of scope for
    // this ticket - see ScenePostProcessAccess.hpp's own doc comment).
    std::uint64_t postProcessProfileAssetId = 0U;
    // LIB-147: scene-global active AudioMixer asset id (0 = none) + active snapshot NAME
    // (empty = authored bus volumes) - the ONLY mixer selection a scene carries, mirroring
    // postProcessProfileAssetId's exact scene-global-toggle shape. Content resolution and
    // application happen entirely in the audio backend each tick - see
    // SceneAudioMixerAccess.hpp's own doc comment.
    std::uint64_t audioMixerAssetId = 0U;
    std::string audioMixerSnapshotName;
    // LIB-150: runtime per-bus volume overrides (strongest layer) + the active snapshot
    // transition (advanced with scene delta time by the audio backend each tick).
    std::vector<AudioMixerBusVolumeOverride> audioMixerBusVolumeOverrides;
    AudioMixerSnapshotTransition audioMixerSnapshotTransition;
    // LIB-151: scene-global audio occlusion configuration (disabled by default - zero
    // raycast cost until a game opts in).
    AudioOcclusionSettings audioOcclusionSettings;
    AudioOcclusionRuntimeStats audioOcclusionRuntimeStats;
    // LIB-152: fired voice markers awaiting script dispatch (mirror of
    // pendingCollisionEvents - queued by the audio backend each tick, drained into
    // ENTITY-LOCAL "OnAudioMarker" events by ScriptRuntimeSceneSystem).
    std::vector<kb::audio::PendingAudioMarkerEvent> pendingAudioMarkerEvents;
    // LIB-127: OnCollisionEnter/Stay/Exit and OnTriggerEnter/Stay/Exit
    // payload, queued by whichever physics plugin is loaded via
    // PhysicsBackend::QueueCollisionEvent - mirrors
    // pendingSceneLifecycleEvents above exactly (see PhysicsBackend.hpp's
    // own doc comment on PendingCollisionEvent for the full contract).
    std::vector<PendingCollisionEvent> pendingCollisionEvents;
    // LIB-160: prefab-instantiation completion notifications. World.
    // InstantiatePrefab queues one per instantiation whose caller is a live
    // entity; ScriptRuntimeSceneSystem drains them each frame into an
    // ENTITY-LOCAL "OnPrefabInstantiated" event targeting the CALLER (the
    // script that requested the spawn) carrying the instantiated root and
    // object count — the same "the operation you started completed, here's
    // the result" shape TaskCompleted/TimerFired use (a synchronous
    // instantiate has no async completion to wait for, so this is the
    // decoupled push channel, not a fabricated async result).
    struct PendingPrefabInstantiatedEvent {
        SceneEntity caller;
        SceneEntity root;
        std::int32_t count = 0;
    };
    std::vector<PendingPrefabInstantiatedEvent> pendingPrefabInstantiatedEvents;
    // LIB-162: the scene's ambient SaveGame buffer — the scalar key/value
    // table the script Save.* surface reads and mutates, and that Save.Write/
    // Save.Read serialize to/from disk. Scene-scoped (reached via Scene::
    // AmbientSave()) so it is accessible through a ScriptFunctionCallContext's
    // scene, mirroring how the rest of the Assets/World/Prefab script APIs
    // reach scene-owned state.
    kb::save::SaveGame ambientSave;
    // LIB-163: the scene's ambient user-settings buffer — a store separate
    // from ambientSave (game progress), serialized under the UserSettings
    // domain so the two persistence categories cannot cross-contaminate.
    kb::save::SaveGame ambientSettings;
    // LIB-129: the last layers configuration applied via
    // PhysicsBackend::ConfigureLayers/LoadAndConfigureLayers - kept here
    // (backend-independent) so name -> bit resolution (PhysicsBackend::
    // LayerBit) works even without a physics backend registered. Default-
    // constructed (layer 0 = "Default", every pair interacts) matches every
    // pre-LIB-129 scene exactly.
    PhysicsLayersAsset physicsLayers;
    // LIB-132: off by default, and never touched by kb_standalone_player's own code path
    // (only the editor's Scene Viewport ever calls PhysicsDebugDraw::SetEnabled/CollectLines)
    // - see PhysicsDebugDraw.hpp's own doc comment for the full "zero release-path impact"
    // argument.
    bool physicsDebugDrawEnabled = false;
    PhysicsDebugQueryTrace physicsDebugQueryTrace{};
    // LIB-143: one live particle system instance started through
    // kb::scene::SceneParticleSystems::Create. `id` is assigned from
    // nextParticleSystemInstanceId, never reused within a scene's lifetime (same convention
    // as nextTimerId/nextMaterialInstanceId above). `owner` follows the exact same
    // auto-release-on-death/deactivation convention SceneTimerService::OwnerGone already
    // establishes (see SceneParticleSystemService.cpp's own copy of that check).
    // `resolvedMaterialAssetId` is resolved once at Create() time (see
    // ParticleEffectAsset.hpp's own doc comment for why); everything else needed for
    // simulation is re-read fresh from the effect asset every Advance() call (keeps hot
    // reload of the .kbvfx file live, mirrors RuntimeMeshResourceEnsurer's own
    // every-frame-Load() convention), with any per-instance `overrides` applied on top.
    struct ParticleSystemParameterOverrides {
        std::optional<float> emissionRatePerSecond;
        std::optional<float> startSpeedMin;
        std::optional<float> startSpeedMax;
        std::optional<float> startLifetimeMin;
        std::optional<float> startLifetimeMax;
        std::optional<float> spreadDegrees;
        std::optional<float> gravityScale;
    };
    struct ParticleSystemInstanceRecord {
        std::uint64_t id = 0U;
        std::uint64_t effectAssetId = 0U;
        std::uint64_t resolvedMaterialAssetId = 0U;
        SceneEntity owner{};
        bool playing = false;
        bool completionArmed = false;
        kb::math::RandomStream rng{};
        // Fractional particle carried between Advance() calls so a non-integer
        // emissionRatePerSecond*deltaSeconds still emits at the correct long-run average
        // rate instead of silently truncating every frame.
        float emissionAccumulator = 0.0F;
        // Seconds since Play() was last called - drives the non-looping `durationSeconds`
        // auto-stop; reset to 0 every Play() call.
        float elapsedSeconds = 0.0F;
        ParticleSystemParameterOverrides overrides;
        std::vector<ParticleState> particles;
    };
    std::vector<ParticleSystemInstanceRecord> particleSystems;
    std::vector<ParticleSystemFinishedEvent> pendingParticleSystemFinishedEvents;
    std::uint64_t nextParticleSystemInstanceId = 1U;
    // LIB-144: the renderer-published per-entity visibility/bounds feedback frame
    // (Renderer.IsVisible/GetBounds/TestFrustum's backing state) - written by
    // kb::render::Renderer at every SubmitScene through SceneRenderFeedback::Publish
    // (through the same const_cast-during-submit convention EnsureSceneResources already
    // uses for asset loading), read by scripts the next frame. renderVisibilityPublishCount
    // is monotonic; 0 means "no frame was ever published" and every query honestly returns
    // its empty result - see SceneRenderFeedback.hpp's own doc comment for the full
    // latency/last-submit-wins contract.
    // One feedback frame per local player. A scene may be submitted to several
    // player-owned viewports in one render frame; keeping only the last submit
    // made pointer rays and camera conversions use another player's camera.
    std::unordered_map<std::uint32_t, SceneRenderVisibilityFrame>
        renderVisibilityFrames;
    std::uint32_t lastRenderVisibilityLocalUserId = 0U;
    std::uint64_t renderVisibilityPublishCount = 0U;
    // LIB-145: the single-slot async screen-capture request/result channel. A script's
    // RequestScreenCapture fills the pending slot (one in-flight per scene, mirroring
    // SceneExposureGpuReadback's own single-pending rule); the renderer consumes it during
    // SubmitScene, performs the frame-gated GPU readback + PNG encode across later frames,
    // and reports through CompleteScreenCapture. Ids are monotonic, never reused.
    std::uint64_t nextScreenCaptureId = 1U;
    std::uint64_t pendingScreenCaptureId = 0U;
    std::string pendingScreenCapturePath;
    bool pendingScreenCaptureConsumed = false;
    std::uint64_t lastScreenCaptureId = 0U;
    bool lastScreenCaptureSucceeded = false;
};

} // namespace kb::scene
