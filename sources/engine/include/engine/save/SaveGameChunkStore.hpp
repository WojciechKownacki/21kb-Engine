#pragma once

#include "engine/save/SaveGameService.hpp"

#include <map>
#include <span>
#include <string>

namespace kb::platform { class UserStorage; }

namespace kb::save {

struct SaveGameChunkUpdate {
    std::string name;
    const SaveGame* save = nullptr; // nullptr removes this chunk from the new snapshot.
};

struct SaveGameChunkSnapshot {
    bool succeeded = false;
    std::string diagnostic;
    std::string slot;
    std::map<std::string, std::string> chunks;
};

// One writer owns a slot. Each chunk retains the ordinary authenticated save
// format and 16 MiB limit. A small authenticated index commits the whole update
// after every changed chunk is written. Readers open an index once, then load
// chunks individually from that immutable snapshot. Web hosts still call Flush
// and wait for clean UserStorage sync before claiming durable persistence.
class SaveGameChunkStore final {
public:
    static constexpr std::size_t MaxChunks = 1024U;
    SaveGameChunkStore(kb::platform::UserStorage& storage, std::string slot);
    [[nodiscard]] SaveGameChunkSnapshot Open() const;
    [[nodiscard]] SaveGameLoadResult Load(const SaveGameChunkSnapshot& snapshot, std::string_view name) const;
    [[nodiscard]] bool Commit(std::span<const SaveGameChunkUpdate> updates, std::string* error = nullptr);
    // Call only when old snapshots are no longer in use. Removes unreferenced
    // chunks left by older generations or interrupted commits; keeps the index.
    [[nodiscard]] bool Prune(std::string* error = nullptr);

private:
    kb::platform::UserStorage& storage_;
    std::string slot_;
};

} // namespace kb::save
