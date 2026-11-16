#pragma once

#include "engine/particles/ParticleGpuEmitter.hpp"
#include "engine/particles/ParticleProcessAsset.hpp"
#include "engine/assets/AssetManager.hpp"
#include "kb/render/particles/ParticleGpuUpdateFailure.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

// A GPU emitter that draws mesh instances: the mesh pipeline binds `instances` (a compute-written buffer in its own
// instance layout, `count` entries) and applies the material.
struct ParticleGpuMeshDraw {
    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
    std::uint32_t count = 0U;
    bool castsShadow = false;
    bool receivesShadow = true;
    // World position the emitter is placed by among the translucent draws (its particles are ordered among
    // themselves on the GPU).
    std::array<float, 3> origin{};
};

// Renderer side of GPU-simulated particle emitters. Owns, per emitter, a ring buffer of birth records
// and the instance buffer a compute pass rebuilds every frame. Normal simulation performs no readback.
class ParticleGpuEmitterSimulation final {
public:
    struct Draw {
        const kb::particles::ParticleGpuEmitterParams* params = nullptr;
        bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
        // Instances in the buffer: the slot count, times the segments per slot of a trail emitter.
        std::uint32_t capacity = 0U;
        std::array<float, 3> origin{};
        kb::particles::ParticleGpuEmitterKey key{};
        // Requested buffer bytes, including birth/state/upload/instance and optional sort storage; not physical residency.
        std::uint64_t requestedBufferBytes = 0U;
        // Ribbon and beam emitters: the strip the strip kernel built this frame (four vertices and six indices per
        // segment, the vertex layout of ParticleStripVertex padded to four floats per attribute) instead of instances.
        bgfx::DynamicVertexBufferHandle stripVertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle stripIndices = BGFX_INVALID_HANDLE;
        std::uint32_t stripSegments = 0U;
    };

    // Logical emitter buffer budget; zero uses the engine limit. Fixed until Shutdown. `collisionEventCapacity` is how
    // many collisions of a scene the kernels can record per frame (see ParticleGpuCollisionBatch).
    [[nodiscard]] bool Initialize(std::uint64_t maxBufferBytes = 0U,
        std::uint32_t collisionEventCapacity = kb::particles::kParticleGpuCollisionEventCapacity);
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept;

    // The render origin of the scene's frame: owner matrices and mesh emitter origins are kept relative to it, and
    // so are the birth and collision records of world-space emitters once the next Dispatch has moved them there
    // (cs_particle_gpu_rebase), so live particles stay where they are in the world when the origin moves.
    void SetRenderOrigin(std::uint64_t sceneId, const kb::math::DVec3& renderOrigin) noexcept;
    // Applies one frame of drained commands for a scene: creates emitters, uploads spawn records,
    // releases emitters. Positions are carried from each command's simulation origin to the render origin.
    void Apply(std::uint64_t sceneId, std::span<const kb::particles::ParticleGpuEmitterCommand> commands, kb::assets::AssetManager* assets = nullptr) noexcept;
    struct RejectedUpdate {
        kb::particles::ParticleGpuEmitterKey key{};
        ParticleGpuUpdateFailure failure = ParticleGpuUpdateFailure::None;
    };
    // Rejections persist until the same emitter receives accepted parameters or is released.
    [[nodiscard]] std::span<const RejectedUpdate> RejectedUpdates(std::uint64_t sceneId) const noexcept;
    // Moves the scene render clock forward by the frame time, never more than one fixed step away
    // from the newest simulated time.
    void Advance(std::uint64_t sceneId, float frameDeltaSeconds) noexcept;
    // What the frame gives the simulation: the scene depth the camera has drawn so far (colliding emitters
    // bounce off it) and the camera data used by the authored particle sort.
    struct FrameContext {
        bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
        std::array<float, 16> viewProjection{};
        std::array<float, 16> inverseViewProjection{};
        std::array<float, 4> cameraPosition{};
        // The view matrix's depth row, including translation, in render space.
        std::array<float, 4> cameraDepth{0.0F, 0.0F, 1.0F, 0.0F};
        std::array<float, 2> texelSize{};
        bool homogeneousDepth = false;
        // Resolve the material of a mesh emitter, whose base colour goes into the instance colour.
        const RenderResourceRegistry* resources = nullptr;
        const SceneRenderResourceMap* resourceMap = nullptr;
        // The camera's right axis (strips are turned across the camera; cameraDepth.xyz is its forward axis).
        std::array<float, 3> cameraRight{1.0F, 0.0F, 0.0F};
    };

    // Rebuilds the instance buffers of the scene emitters; call once per rendered frame.
    void Dispatch(bgfx::ViewId viewId, std::uint64_t sceneId, const FrameContext& frame) noexcept;
    // Collisions recorded by the kernels of emitters that report them (ParticleGpuEmitterParams::collisionEvents).
    // Dispatch asks bgfx to copy the scene's event buffer back; the copy is complete two frames later. This moves the
    // copies that bgfx has completed by `completedFrame` (what bgfx::frame returned) into `output`, oldest first, and
    // never waits for the rest.
    void CollectCollisionEvents(std::uint64_t sceneId, std::uint32_t completedFrame,
        kb::particles::ParticleGpuCollisionBatch& output) noexcept;
    struct CollisionEventStats {
        std::uint64_t recorded = 0U;   // hits the kernels counted in the copies collected so far
        std::uint64_t delivered = 0U;  // of them, the records that fitted the buffer
        std::uint64_t lost = 0U;       // of them, the ones beyond its capacity
        std::uint32_t submittedReads = 0U;
        std::uint32_t pendingReads = 0U;
        std::uint32_t skippedReads = 0U; // frames whose copy could not be issued because none had been collected
    };
    [[nodiscard]] CollisionEventStats CollisionStats(std::uint64_t sceneId) const noexcept;
    // Emitters of the scene that were dispatched this frame.
    [[nodiscard]] std::span<const Draw> Draws(std::uint64_t sceneId) const noexcept;
    [[nodiscard]] bool HasEmitters(std::uint64_t sceneId) const noexcept;

    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllScenes() noexcept;
    [[nodiscard]] std::uint64_t AllocatedBytes() const noexcept { return allocatedBytes_; }

private:
    struct Key {
        std::uint64_t sceneId = 0U;
        std::uint64_t instanceId = 0U;
        std::uint64_t emitterId = 0U;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };
    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            std::uint64_t hash = key.sceneId * 0x9E3779B97F4A7C15ULL;
            hash ^= key.instanceId + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
            hash ^= key.emitterId + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
            return static_cast<std::size_t>(hash);
        }
    };
    struct Emitter {
        kb::particles::ParticleGpuEmitterParams params{};
        // Owner matrix of a local-space emitter (identity for world-space ones) and its inverse.
        std::array<float, 16> world{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
        std::array<float, 16> worldInverse = world;
        // Columns of the rotation matrix mesh particles are oriented by (identity until the owner's orientation arrives).
        std::array<float, 3> origin{};
        std::array<float, 12> basis{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
        // Fields of an emitter with forces, in the frame of its records (ParticleGpuEmitterCommand::hasField).
        kb::math::Vec3 fieldOrigin{};
        bgfx::DynamicVertexBufferHandle spawns = BGFX_INVALID_HANDLE;
        // CPU-writable birth records copied into the GPU-writable ring before compute.
        bgfx::DynamicVertexBufferHandle spawnUpload = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
        // Per-slot position/velocity record of the stateful kernel; only created for emitters that need it
        // (collisions, forces).
        bgfx::DynamicVertexBufferHandle state = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle stateZeros = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle customProgram = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle customState = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle customZeros = BGFX_INVALID_HANDLE;
        std::shared_ptr<const kb::particles::ParticleProcessAsset> customAsset;
        bool initializeCustom = false;
        std::uint64_t customBytes = 0U;
        // Alpha-blended emitters only: sort keys (distance, slot) padded to a power of two, and the
        // instance records in back-to-front order, which is what gets drawn.
        bgfx::DynamicVertexBufferHandle sortKeys = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle sortedInstances = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
        // Instances written per slot: 1, or the trail segments.
        std::uint32_t perSlot = 1U;
        // Ribbon and beam emitters: segments of the strip (the ring slots of a ribbon), its vertices, the fixed indices.
        std::uint32_t stripSegments = 0U;
        bgfx::DynamicVertexBufferHandle stripVertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle stripIndices = BGFX_INVALID_HANDLE;
        // Beam ends in render space.
        std::array<float, 3> beamStart{};
        std::array<float, 3> beamEnd{};
        std::uint64_t nextSlot = 0U;
        std::uint32_t pendingBirths = 0U;
        bool initializeSpawns = true;
        bool initializeState = false;
        std::uint64_t bytes = 0U;
    };
    // The CPU side of one copy of the event buffer: bgfx writes into `words` when its frame completes.
    struct EventReadback {
        std::vector<std::uint32_t> words;
        std::vector<kb::particles::ParticleGpuEmitterKey> emitters; // index in a record -> emitter
        kb::math::DVec3 origin{};
        std::uint32_t readyFrame = 0U;
        bool pending = false;
    };
    static constexpr std::size_t kEventReadbackRing = 4U;
    struct SceneClock {
        double latest = 0.0;
        double now = 0.0;
        kb::math::DVec3 renderOrigin{};
        // The origin the GPU records of world-space emitters (and their collision planes) are relative to: the
        // render origin as of the last Dispatch. Records uploaded before the next Dispatch are converted to it.
        kb::math::DVec3 recordOrigin{};
        std::vector<Draw> draws;
        std::vector<RejectedUpdate> rejections;
        // Event buffer shared by the scene's colliding emitters: the kernels append to it, the counter is cleared by a
        // copy from `eventZeros` before each dispatch, and the whole buffer is copied back after it.
        bgfx::DynamicIndexBufferHandle eventRecords = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle eventZeros = BGFX_INVALID_HANDLE;
        std::array<std::shared_ptr<EventReadback>, kEventReadbackRing> eventReads;
        std::vector<kb::particles::ParticleGpuEmitterKey> eventEmitters;
        CollisionEventStats eventStats;
    };

    [[nodiscard]] bool Create(Emitter& emitter, const kb::particles::ParticleGpuEmitterParams& params) noexcept;
    [[nodiscard]] bool EnsureCustomProcess(Emitter& emitter, std::uint64_t assetId, kb::assets::AssetManager* assets) noexcept;
    void Destroy(Emitter& emitter) noexcept;
    void Upload(Emitter& emitter, std::span<const kb::particles::ParticleGpuSpawn> spawns) noexcept;
    void Clear(Emitter& emitter) noexcept;
    void FlushUploads(bgfx::ViewId viewId, Emitter& emitter) noexcept;
    // Creates the state buffer of a colliding emitter; false when the memory budget would be exceeded.
    [[nodiscard]] bool EnsureState(Emitter& emitter) noexcept;
    // Creates the sort buffers of an alpha-blended emitter; false when the budget or a kernel is missing.
    [[nodiscard]] bool EnsureSort(Emitter& emitter) noexcept;
    void SortInstances(bgfx::ViewId viewId, const Emitter& emitter, const FrameContext& frame, float simTime) noexcept;
    // Writes the strip vertices of a ribbon (from the instance records) or a beam (from its ends) in the view.
    void BuildStrip(bgfx::ViewId viewId, const Emitter& emitter, const FrameContext& frame, float simTime) noexcept;
    // Creates the scene's event buffer; false when bgfx could not.
    [[nodiscard]] bool EnsureCollisionEvents(SceneClock& clock) noexcept;
    void ReadBackCollisionEvents(SceneClock& clock) noexcept;
    // Releases the scene's event buffer; copies still in flight keep their CPU storage until bgfx has written them.
    void DestroyCollisionEvents(SceneClock& clock) noexcept;
    // Moves the records of the scene's world-space emitters from the record origin to the render origin.
    void Rebase(bgfx::ViewId viewId, std::uint64_t sceneId, SceneClock& clock) noexcept;
    // Whether the draw order of this emitter's instances matters: alpha-blended billboards, and mesh particles
    // whose material is translucent (an opaque mesh is depth-tested and needs no order).
    [[nodiscard]] static bool NeedsSort(const kb::particles::ParticleGpuEmitterParams& params, bool translucentMaterial) noexcept;

    std::vector<kb::particles::ParticleGpuSpawn> spawnScratch_;
    std::vector<std::shared_ptr<EventReadback>> retiredReads_;
    std::uint32_t collisionEventCapacity_ = 0U;
    std::uint64_t maxBufferBytes_ = 0U;
    bgfx::UniformHandle processFrameUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle processInverseUniform_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    // Required for the GPU consumer: absence keeps authored collision/force semantics on the CPU.
    bgfx::ProgramHandle collideProgram_ = BGFX_INVALID_HANDLE;
    // Optional: without it the live particles of world-space emitters are cleared when the render origin moves.
    bgfx::ProgramHandle rebaseProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle rebaseUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle worldUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle worldInverseUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle localUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle planeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle collisionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthBounceUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texelUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackDepth_ = BGFX_INVALID_HANDLE;
    // Optional kernels of the authored particle sort (all three are needed).
    bgfx::ProgramHandle sortKeysProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sortStepProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sortGatherProgram_ = BGFX_INVALID_HANDLE;
    // Strip kernel of ribbon and beam emitters; without it those emitters are rejected (the CPU builds them).
    bgfx::ProgramHandle stripProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle stripModeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle stripBeamUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle stripCameraUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sortCameraUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sortDepthUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sortParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle timeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sizeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle outputUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle flipbookUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle spinUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fieldUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle forcesUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle noiseOriginUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle eventUniform_ = BGFX_INVALID_HANDLE;
    std::unordered_map<Key, Emitter, KeyHash> emitters_;
    std::unordered_map<std::uint64_t, SceneClock> scenes_;
    std::uint64_t allocatedBytes_ = 0U;
};

} // namespace kb::render
