#include "engine/assets/streaming/StreamingResidency.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>

namespace kb::assets::streaming {
namespace {

constexpr std::uint32_t kMaxLevels = 32U;
// A resource nobody asked for this long wants nothing above its floor any more, which makes
// its streamed levels the first thing a load may evict.
constexpr std::uint64_t kStaleAfterFrames = 120U;

[[nodiscard]] std::uint32_t LevelBit(std::uint32_t level) noexcept {
    return 1U << level;
}

// Bits of every level finer than `level`.
[[nodiscard]] std::uint32_t FinerThan(std::uint32_t level) noexcept {
    return level == 0U ? 0U : (LevelBit(level) - 1U);
}

} // namespace

bool StreamingResidencyManager::Register(StreamingResourceId id, const StreamingResourceDesc& desc) {
    if (desc.levelBytes.empty() || desc.levelBytes.size() > kMaxLevels ||
        desc.residentFloor >= desc.levelBytes.size() || resources_.contains(id)) {
        return false;
    }
    Resource resource{};
    resource.levelBytes = desc.levelBytes;
    resource.floor = desc.residentFloor;
    resource.residentLevel = desc.residentFloor;
    resource.wantedLevel = desc.residentFloor;
    resource.retryFrame.assign(desc.levelBytes.size(), 0U);
    const std::uint64_t floorBytes = std::accumulate(
        desc.levelBytes.begin() + desc.residentFloor, desc.levelBytes.end(), std::uint64_t{ 0U });
    floorBytes_ += floorBytes;
    residentBytes_ += floorBytes;
    resources_.emplace(id, std::move(resource));
    NoteCommitted();
    return true;
}

void StreamingResidencyManager::Unregister(StreamingResourceId id) {
    const auto found = resources_.find(id);
    if (found == resources_.end()) {
        return;
    }
    const Resource& resource = found->second;
    for (std::uint32_t level = 0U; level < resource.levelBytes.size(); ++level) {
        if (level >= resource.residentLevel || (resource.arrived & LevelBit(level)) != 0U) {
            residentBytes_ -= resource.levelBytes[level];
        }
        if ((resource.inFlight & LevelBit(level)) != 0U) {
            inFlightBytes_ -= resource.levelBytes[level];
        }
    }
    floorBytes_ -= std::accumulate(
        resource.levelBytes.begin() + resource.floor, resource.levelBytes.end(), std::uint64_t{ 0U });
    resources_.erase(found);
}

bool StreamingResidencyManager::IsRegistered(StreamingResourceId id) const noexcept {
    return resources_.contains(id);
}

void StreamingResidencyManager::Request(StreamingResourceId id, std::uint32_t wantedLevel, float priority, std::uint64_t frame) {
    const auto found = resources_.find(id);
    if (found == resources_.end()) {
        return;
    }
    Resource& resource = found->second;
    resource.wantedLevel = std::min(wantedLevel, resource.floor);
    resource.priority = priority > 0.0F ? priority : 0.0F;
    resource.lastRequestedFrame = std::max(resource.lastRequestedFrame, frame);
}

void StreamingResidencyManager::NoteCommitted() noexcept {
    counters_.peakCommittedBytes = std::max(counters_.peakCommittedBytes, Committed());
}

void StreamingResidencyManager::Promote(Resource& resource) {
    while (resource.residentLevel > 0U && (resource.arrived & LevelBit(resource.residentLevel - 1U)) != 0U) {
        --resource.residentLevel;
        resource.arrived &= ~LevelBit(resource.residentLevel);
    }
}

bool StreamingResidencyManager::EvictOne(StreamingResourceId id, Resource& resource, StreamingPlan& plan) {
    if (resource.residentLevel >= resource.floor) {
        return false;
    }
    // Dropping this level while a finer one is loading or waiting would leave a hole that
    // finer level could never be promoted across.
    if (((resource.inFlight | resource.arrived) & FinerThan(resource.residentLevel)) != 0U) {
        return false;
    }
    const std::uint64_t bytes = resource.levelBytes[resource.residentLevel];
    residentBytes_ -= bytes;
    ++resource.residentLevel;
    ++counters_.evictions;
    counters_.evictedBytes += bytes;
    if (!plan.evictions.empty() && plan.evictions.back().resource == id) {
        plan.evictions.back().newResidentLevel = resource.residentLevel;
        plan.evictions.back().bytes += bytes;
    } else {
        plan.evictions.push_back(StreamingEviction{ id, resource.residentLevel, bytes });
    }
    return true;
}

StreamingPlan StreamingResidencyManager::Plan(std::uint64_t frame, const StreamingPlanLimits& limits) {
    StreamingPlan plan;
    retryAfterFrames_ = limits.retryAfterFrames;
    const auto effectiveWanted = [frame](const Resource& resource) {
        const bool stale = frame > resource.lastRequestedFrame && frame - resource.lastRequestedFrame > kStaleAfterFrames;
        return stale ? resource.floor : resource.wantedLevel;
    };

    struct Victim {
        StreamingResourceId id = 0U;
        Resource* resource = nullptr;
    };
    // Levels finer than their resource wants: the least recently wanted go first.
    const auto overResident = [&] {
        std::vector<Victim> victims;
        for (auto& [id, resource] : resources_) {
            if (resource.residentLevel < effectiveWanted(resource)) {
                victims.push_back(Victim{ id, &resource });
            }
        }
        std::ranges::sort(victims, [](const Victim& lhs, const Victim& rhs) {
            if (lhs.resource->lastRequestedFrame != rhs.resource->lastRequestedFrame) {
                return lhs.resource->lastRequestedFrame < rhs.resource->lastRequestedFrame;
            }
            if (lhs.resource->priority != rhs.resource->priority) {
                return lhs.resource->priority < rhs.resource->priority;
            }
            return lhs.id < rhs.id;
        });
        return victims;
    };
    // Streamed levels of resources less important than `priority`: lowest priority first, the
    // least recently wanted among equals.
    const auto lessImportant = [&](float priority, StreamingResourceId except) {
        std::vector<Victim> victims;
        for (auto& [id, resource] : resources_) {
            if (id != except && resource.residentLevel < resource.floor && resource.priority < priority) {
                victims.push_back(Victim{ id, &resource });
            }
        }
        std::ranges::sort(victims, [](const Victim& lhs, const Victim& rhs) {
            if (lhs.resource->priority != rhs.resource->priority) {
                return lhs.resource->priority < rhs.resource->priority;
            }
            if (lhs.resource->lastRequestedFrame != rhs.resource->lastRequestedFrame) {
                return lhs.resource->lastRequestedFrame < rhs.resource->lastRequestedFrame;
            }
            return lhs.id < rhs.id;
        });
        return victims;
    };
    const auto evictUntil = [&](std::uint64_t needed, const std::vector<Victim>& victims) {
        for (const Victim& victim : victims) {
            while (Committed() + needed > budgetBytes_ && EvictOne(victim.id, *victim.resource, plan)) {
            }
            if (Committed() + needed <= budgetBytes_) {
                return true;
            }
        }
        return Committed() + needed <= budgetBytes_;
    };

    // A budget that shrank is honoured before anything new starts.
    if (Committed() > budgetBytes_) {
        static_cast<void>(evictUntil(0U, overResident()));
        static_cast<void>(evictUntil(0U, lessImportant(std::numeric_limits<float>::infinity(), 0U)));
    }

    struct Candidate {
        StreamingResourceId id = 0U;
        std::uint32_t level = 0U;
        float priority = 0.0F;
    };
    std::vector<Candidate> candidates;
    std::uint32_t inFlightCount = 0U;
    for (const auto& [id, resource] : resources_) {
        inFlightCount += static_cast<std::uint32_t>(std::popcount(resource.inFlight));
        const std::uint32_t wanted = effectiveWanted(resource);
        for (std::uint32_t level = resource.residentLevel; level > wanted;) {
            --level;
            if (((resource.inFlight | resource.arrived) & LevelBit(level)) != 0U || resource.retryFrame[level] > frame) {
                continue;
            }
            candidates.push_back(Candidate{ id, level, resource.priority });
        }
    }
    std::ranges::sort(candidates, [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
        }
        return lhs.level > rhs.level;
    });

    std::uint64_t startedBytes = 0U;
    std::vector<StreamingResourceId> blocked;
    for (const Candidate& candidate : candidates) {
        if (inFlightCount >= limits.maxLoadsInFlight) {
            break;
        }
        if (std::ranges::find(blocked, candidate.id) != blocked.end()) {
            continue;
        }
        Resource& resource = resources_.at(candidate.id);
        const std::uint64_t bytes = resource.levelBytes[candidate.level];
        if (limits.maxBytesStartedPerPlan != 0U && startedBytes != 0U &&
            startedBytes + bytes > limits.maxBytesStartedPerPlan) {
            break;
        }
        if (Committed() + bytes > budgetBytes_ &&
            !evictUntil(bytes, overResident()) &&
            !evictUntil(bytes, lessImportant(candidate.priority, candidate.id))) {
            // Its finer levels could not become resident before this one anyway.
            blocked.push_back(candidate.id);
            continue;
        }
        resource.inFlight |= LevelBit(candidate.level);
        inFlightBytes_ += bytes;
        startedBytes += bytes;
        ++inFlightCount;
        ++counters_.loadsStarted;
        NoteCommitted();
        plan.loads.push_back(StreamingLoad{ candidate.id, candidate.level, bytes, candidate.priority });
    }

    std::uint32_t starved = 0U;
    for (const auto& [id, resource] : resources_) {
        const std::uint32_t wanted = effectiveWanted(resource);
        for (std::uint32_t level = wanted; level < resource.residentLevel; ++level) {
            if (((resource.inFlight | resource.arrived) & LevelBit(level)) == 0U) {
                ++starved;
                break;
            }
        }
    }
    counters_.starvedResources = starved;
    return plan;
}

void StreamingResidencyManager::CompleteLoad(StreamingResourceId id, std::uint32_t level, bool succeeded, std::uint64_t frame) {
    const auto found = resources_.find(id);
    if (found == resources_.end()) {
        return;
    }
    Resource& resource = found->second;
    if (level >= resource.levelBytes.size() || (resource.inFlight & LevelBit(level)) == 0U) {
        return;
    }
    const std::uint64_t bytes = resource.levelBytes[level];
    resource.inFlight &= ~LevelBit(level);
    inFlightBytes_ -= bytes;
    if (!succeeded) {
        ++counters_.loadsFailed;
        resource.retryFrame[level] = frame + retryAfterFrames_;
        return;
    }
    ++counters_.loadsCompleted;
    residentBytes_ += bytes;
    resource.arrived |= LevelBit(level);
    Promote(resource);
    NoteCommitted();
}

std::uint32_t StreamingResidencyManager::ResidentLevel(StreamingResourceId id) const noexcept {
    const auto found = resources_.find(id);
    return found == resources_.end() ? 0U : found->second.residentLevel;
}

std::uint32_t StreamingResidencyManager::WantedLevel(StreamingResourceId id) const noexcept {
    const auto found = resources_.find(id);
    return found == resources_.end() ? 0U : found->second.wantedLevel;
}

bool StreamingResidencyManager::IsLoadInFlight(StreamingResourceId id, std::uint32_t level) const noexcept {
    const auto found = resources_.find(id);
    return found != resources_.end() && level < kMaxLevels && (found->second.inFlight & LevelBit(level)) != 0U;
}

StreamingResidencyStats StreamingResidencyManager::Stats() const noexcept {
    StreamingResidencyStats stats = counters_;
    stats.budgetBytes = budgetBytes_;
    stats.residentBytes = residentBytes_;
    stats.inFlightBytes = inFlightBytes_;
    stats.floorBytes = floorBytes_;
    stats.resources = static_cast<std::uint32_t>(resources_.size());
    return stats;
}

} // namespace kb::assets::streaming
