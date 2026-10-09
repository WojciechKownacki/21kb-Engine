#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// Which levels of detail of which streamed resources are resident, under ONE memory budget.
//
// A streamed resource -- a texture's mip chain, a mesh's LOD chain -- is a list of levels, finest
// (level 0) first, each with the bytes it occupies when resident. The coarsest levels from
// `residentFloor` on are loaded with the resource and never evicted: a texture always has its mip
// tail, a mesh its coarsest LOD, so something is always there to draw. Every finer level is
// streamed: resident levels are always a contiguous run residentLevel..levelCount-1.
//
// Each frame the owner (the renderer) reports, per resource it drew, the finest level it wants
// and a priority -- how much of the screen the resource covers, for example. Plan() then decides
// which level loads to start and which levels to evict:
//
//  * The budget is never exceeded. Bytes are COMMITTED when a load is issued, not when it lands,
//    so the resident bytes plus the bytes of every load in flight stay within the budget. Only
//    the never-evicted floors can exceed it (FloorBytes), and then nothing else streams.
//  * Loads are issued highest priority first; within a resource coarser levels go first, so a
//    texture sharpens from its tail upwards and something better arrives every step.
//  * When a load does not fit, space is made by evicting -- LRU by priority. First levels finer
//    than their resource now wants (the camera moved away), least recently wanted first; then,
//    if still short, wanted levels of resources whose priority is lower than the load's,
//    lowest priority first and least recently wanted among equals. A load never evicts
//    anything as important as itself, so two resources cannot thrash each other.
//  * A level is evicted one at a time from the fine end, and never while a finer load of the
//    same resource is in flight (it would leave a hole in the run).
//
// The manager does no I/O. Plan() hands back what to load and what to drop; the owner starts the
// loads (asynchronously), reports each completion with CompleteLoad, and applies evictions to
// its GPU resources. Plan's evictions are already accounted for when it returns.
namespace kb::assets::streaming {

using StreamingResourceId = std::uint64_t;

struct StreamingResourceDesc {
    // Bytes of each level when resident, finest first. At most 32 levels.
    std::vector<std::uint64_t> levelBytes;
    // First level that is always resident (the floor): levelBytes.size() - 1 for a resource
    // whose coarsest level alone is permanent, 0 for one that does not stream at all.
    std::uint32_t residentFloor = 0U;
};

struct StreamingLoad {
    StreamingResourceId resource = 0U;
    std::uint32_t level = 0U;
    std::uint64_t bytes = 0U;
    float priority = 0.0F;
};

struct StreamingEviction {
    StreamingResourceId resource = 0U;
    // The finest level that stays resident after the eviction.
    std::uint32_t newResidentLevel = 0U;
    std::uint64_t bytes = 0U;
};

struct StreamingPlan {
    std::vector<StreamingLoad> loads;
    std::vector<StreamingEviction> evictions;
};

struct StreamingPlanLimits {
    // Loads that may be in flight at once, across all resources.
    std::uint32_t maxLoadsInFlight = 16U;
    // Bytes of new loads one Plan call may start; 0 means no limit beyond the budget.
    std::uint64_t maxBytesStartedPerPlan = 0U;
    // Frames a failed level waits before it is tried again.
    std::uint64_t retryAfterFrames = 30U;
};

struct StreamingResidencyStats {
    std::uint64_t budgetBytes = 0U;
    // Bytes of resident levels, floors included.
    std::uint64_t residentBytes = 0U;
    // Bytes reserved by loads in flight.
    std::uint64_t inFlightBytes = 0U;
    // The never-evicted floors alone.
    std::uint64_t floorBytes = 0U;
    // Largest residentBytes + inFlightBytes ever reached.
    std::uint64_t peakCommittedBytes = 0U;
    std::uint64_t loadsStarted = 0U;
    std::uint64_t loadsCompleted = 0U;
    std::uint64_t loadsFailed = 0U;
    std::uint64_t evictions = 0U;
    std::uint64_t evictedBytes = 0U;
    // Wanted levels not resident after the last Plan, and how many of them waited for budget.
    std::uint32_t starvedResources = 0U;
    std::uint32_t resources = 0U;
};

class StreamingResidencyManager {
public:
    explicit StreamingResidencyManager(std::uint64_t budgetBytes) noexcept
        : budgetBytes_{ budgetBytes } {}

    // A resource starts with its floor resident. False for an id already registered or a
    // malformed description.
    bool Register(StreamingResourceId id, const StreamingResourceDesc& desc);
    // Forgets the resource and its bytes. A load still in flight for it completes into nothing.
    void Unregister(StreamingResourceId id);
    [[nodiscard]] bool IsRegistered(StreamingResourceId id) const noexcept;

    // The finest level the owner wants this frame and how much it matters (>= 0). A resource not
    // requested in a frame keeps its last wish but ages for LRU purposes.
    void Request(StreamingResourceId id, std::uint32_t wantedLevel, float priority, std::uint64_t frame);

    // Decides loads and evictions for this frame. See the class comment.
    [[nodiscard]] StreamingPlan Plan(std::uint64_t frame, const StreamingPlanLimits& limits = {});

    // A load from Plan finished. On success the level is resident once every coarser streamed
    // level is; on failure its bytes are released and it is retried later.
    void CompleteLoad(StreamingResourceId id, std::uint32_t level, bool succeeded, std::uint64_t frame);

    // Finest resident level (the floor when nothing streamed is in).
    [[nodiscard]] std::uint32_t ResidentLevel(StreamingResourceId id) const noexcept;
    [[nodiscard]] std::uint32_t WantedLevel(StreamingResourceId id) const noexcept;
    [[nodiscard]] bool IsLoadInFlight(StreamingResourceId id, std::uint32_t level) const noexcept;

    void SetBudget(std::uint64_t budgetBytes) noexcept {
        budgetBytes_ = budgetBytes;
    }
    [[nodiscard]] std::uint64_t Budget() const noexcept {
        return budgetBytes_;
    }
    [[nodiscard]] StreamingResidencyStats Stats() const noexcept;

private:
    struct Resource {
        std::vector<std::uint64_t> levelBytes;
        std::uint32_t floor = 0U;
        std::uint32_t residentLevel = 0U;
        std::uint32_t wantedLevel = 0U;
        float priority = 0.0F;
        std::uint64_t lastRequestedFrame = 0U;
        // Bit per level: a load is in flight / the bytes arrived but a coarser level has not.
        std::uint32_t inFlight = 0U;
        std::uint32_t arrived = 0U;
        // Frame before which a failed level is not retried, per level.
        std::vector<std::uint64_t> retryFrame;
    };

    [[nodiscard]] std::uint64_t Committed() const noexcept {
        return residentBytes_ + inFlightBytes_;
    }
    void NoteCommitted() noexcept;
    // Drops the finest resident level of `resource`; false if it has none to give.
    bool EvictOne(StreamingResourceId id, Resource& resource, StreamingPlan& plan);
    void Promote(Resource& resource);

    std::unordered_map<StreamingResourceId, Resource> resources_;
    std::uint64_t budgetBytes_ = 0U;
    std::uint64_t residentBytes_ = 0U;
    std::uint64_t inFlightBytes_ = 0U;
    std::uint64_t floorBytes_ = 0U;
    std::uint64_t retryAfterFrames_ = 30U;
    StreamingResidencyStats counters_{};
};

} // namespace kb::assets::streaming
