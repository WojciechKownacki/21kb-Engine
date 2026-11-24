#pragma once

#include "ReplicationSlots.hpp"

#include <cmath>
#include <unordered_map>

namespace kb::net {

class SpatialGrid final {
public:
    SpatialGrid(ReplicationSlots& slots, float radius) : slots_(&slots), radius_(radius) {}

    void Move(std::uint32_t index, float x, float y) {
        auto& slot = *slots_->At(index);
        slot.x = x;
        slot.y = y;
        slot.hasPosition = std::isfinite(x) && std::isfinite(y);
        if (radius_ <= 0.0F || !slot.hasPosition) { Remove(index); return; }
        const auto key = Key(Coordinate(x), Coordinate(y));
        if (slot.inGrid && slot.cellKey == key) return;
        Remove(index);
        auto& cell = cells_[key];
        slot.cellKey = key;
        slot.cellPosition = static_cast<std::uint32_t>(cell.slots.size());
        slot.inGrid = true;
        cell.slots.push_back(index);
    }

    void Remove(std::uint32_t index) {
        auto& slot = *slots_->At(index);
        if (!slot.inGrid) return;
        auto found = cells_.find(slot.cellKey);
        auto& entries = found->second.slots;
        entries[slot.cellPosition] = entries.back();
        slots_->At(entries.back())->cellPosition = slot.cellPosition;
        entries.pop_back();
        if (entries.empty()) cells_.erase(found);
        slot.inGrid = false;
    }

    void Candidates(float x, float y, std::vector<std::uint32_t>& out) const {
        const auto cx = Coordinate(x), cy = Coordinate(y);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto nx = static_cast<std::int64_t>(cx) + dx, ny = static_cast<std::int64_t>(cy) + dy;
                if (nx < INT32_MIN || nx > INT32_MAX || ny < INT32_MIN || ny > INT32_MAX) continue;
                const auto found = cells_.find(Key(static_cast<std::int32_t>(nx), static_cast<std::int32_t>(ny)));
                if (found != cells_.end()) out.insert(out.end(), found->second.slots.begin(), found->second.slots.end());
            }
        }
    }

private:
    [[nodiscard]] std::int32_t Coordinate(float value) const {
        const double cell = std::floor(static_cast<double>(value) / radius_);
        return static_cast<std::int32_t>(std::clamp(cell, static_cast<double>(INT32_MIN), static_cast<double>(INT32_MAX)));
    }
    [[nodiscard]] static std::uint64_t Key(std::int32_t x, std::int32_t y) noexcept {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U | static_cast<std::uint32_t>(y);
    }
    struct Cell { std::vector<std::uint32_t> slots; };
    ReplicationSlots* slots_;
    float radius_;
    std::unordered_map<std::uint64_t, Cell> cells_;
};

} // namespace kb::net
