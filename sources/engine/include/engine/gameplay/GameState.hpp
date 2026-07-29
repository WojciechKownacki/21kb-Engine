#pragma once

#include <array>
#include <cstdint>

namespace kb::gameplay {

// Plain value transferred by a replication transport. It has no entity ids or
// pointers, so an older scene cannot leak ownership into a new match.
struct GameStateSnapshot {
    std::uint64_t revision = 0U;
    float elapsedSeconds = 0.0F;
    std::array<std::int32_t, 2U> scores{};
    bool matchInProgress = false;
};

class GameState final {
public:
    [[nodiscard]] const GameStateSnapshot& Snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] bool SetMatchInProgress(bool value) noexcept;
    [[nodiscard]] bool Advance(float deltaSeconds) noexcept;
    [[nodiscard]] bool SetScore(std::uint32_t team, std::int32_t score) noexcept;
    // Rejects stale snapshots, so packet reordering cannot rewind match state.
    [[nodiscard]] bool ApplyReplication(const GameStateSnapshot& snapshot) noexcept;

private:
    void Touch() noexcept { ++snapshot_.revision; }
    GameStateSnapshot snapshot_{};
};

} // namespace kb::gameplay
