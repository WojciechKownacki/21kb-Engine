#include "engine/save/SaveGameChunkStore.hpp"

#include "engine/platform/UserStorage.hpp"

#include <algorithm>
#include <set>

namespace kb::save {
namespace {
constexpr std::size_t kIndexLimit = 256U << 10U;

std::span<const std::uint8_t> Bytes(std::string_view text) {
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

bool HashName(std::string_view hash) {
    return hash.size() == 2U * kb::security::kSha512Bytes &&
        std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

std::string ChunkPath(std::string_view slot, std::string_view hash) {
    return std::string{ slot } + "/chunks/" + std::string{ hash } + ".kbsave";
}
} // namespace

SaveGameChunkStore::SaveGameChunkStore(kb::platform::UserStorage& storage, std::string slot)
    : storage_(storage), slot_(std::move(slot)) {}

SaveGameChunkSnapshot SaveGameChunkStore::Open() const {
    SaveGameChunkSnapshot result{ .slot = slot_ };
    const auto fail = [&](std::string message) { result.diagnostic = std::move(message); return result; };
    if (!kb::platform::IsUserStorageSlotName(slot_)) return fail("invalid chunk-store slot");
    const std::string indexPath = slot_ + "/index.kbsave";
    const auto bytes = storage_.ReadBounded(indexPath, kIndexLimit);
    if (!bytes) {
        const auto files = storage_.List();
        if (std::ranges::find(files, indexPath) != files.end()) return fail("chunk index could not be read within its size limit");
        result.succeeded = true;
        return result;
    }
    const auto decoded = SaveGameService::Deserialize(Bytes(*bytes));
    if (!decoded.Succeeded() || decoded.migrated) return fail("chunk index must use the authenticated current format: " + decoded.diagnostic);
    std::int64_t version = 0;
    std::string slot;
    if (!decoded.save.GetInt("archive.version", version) || version != 1 ||
        !decoded.save.GetString("archive.slot", slot) || slot != slot_) return fail("chunk index identity or version mismatch");
    for (const auto& [key, value] : decoded.save.Entries()) {
        if (key == "archive.version" || key == "archive.slot") continue;
        if (!key.starts_with("chunk.") || !kb::platform::IsUserStorageSlotName(std::string_view{ key }.substr(6U)) ||
            value.type != SaveValueType::String || !HashName(value.stringValue)) return fail("invalid chunk index entry");
        result.chunks.emplace(key.substr(6U), value.stringValue);
        if (result.chunks.size() > MaxChunks) return fail("chunk count exceeds its limit");
    }
    result.succeeded = true;
    return result;
}

SaveGameLoadResult SaveGameChunkStore::Load(const SaveGameChunkSnapshot& snapshot, std::string_view name) const {
    const auto fail = [](std::string message) {
        return SaveGameLoadResult{ .status = SaveGameLoadStatus::Corrupt, .diagnostic = std::move(message) };
    };
    if (!snapshot.succeeded || snapshot.slot != slot_ || !kb::platform::IsUserStorageSlotName(slot_) ||
        !kb::platform::IsUserStorageSlotName(name)) return fail("invalid chunk snapshot or name");
    const auto found = snapshot.chunks.find(std::string{ name });
    if (found == snapshot.chunks.end()) return { .status = SaveGameLoadStatus::FileNotFound, .diagnostic = "chunk is absent from this snapshot" };
    if (!HashName(found->second)) return fail("invalid chunk digest");
    const auto bytes = storage_.ReadBounded(ChunkPath(slot_, found->second), SaveGameService::MaxSerializedBytes);
    if (!bytes) return fail("chunk is missing, unreadable or oversized");
    if (kb::security::ToHex(kb::security::Sha512(Bytes(*bytes))) != found->second) return fail("chunk differs from the committed snapshot");
    auto result = SaveGameService::Deserialize(Bytes(*bytes));
    if (result.migrated) return fail("chunk must use the authenticated current format");
    return result;
}

bool SaveGameChunkStore::Commit(std::span<const SaveGameChunkUpdate> updates, std::string* error) {
    if (error != nullptr) error->clear();
    if (updates.size() > 2U * MaxChunks) return Fail(error, "too many chunk updates");
    auto snapshot = Open();
    if (!snapshot.succeeded) return Fail(error, snapshot.diagnostic);
    std::set<std::string> names;
    for (const auto& update : updates) {
        if (!kb::platform::IsUserStorageSlotName(update.name) || !names.insert(update.name).second)
            return Fail(error, "invalid or repeated chunk name");
        if (update.save == nullptr) snapshot.chunks.erase(update.name);
        else snapshot.chunks.try_emplace(update.name, "");
    }
    if (snapshot.chunks.size() > MaxChunks) return Fail(error, "chunk count exceeds its limit");
    for (const auto& update : updates) {
        if (update.save == nullptr) continue;
        const auto bytes = SaveGameService::Serialize(*update.save);
        if (!bytes) return Fail(error, "chunk exceeds save format limits");
        const auto hash = kb::security::ToHex(kb::security::Sha512(*bytes));
        if (!storage_.Write(ChunkPath(slot_, hash),
                { reinterpret_cast<const char*>(bytes->data()), bytes->size() })) return Fail(error, "chunk write failed; index was not published");
        snapshot.chunks[update.name] = hash;
    }
    SaveGame index;
    index.SetInt("archive.version", 1);
    index.SetString("archive.slot", slot_);
    for (const auto& [name, hash] : snapshot.chunks) index.SetString("chunk." + name, hash);
    const auto bytes = SaveGameService::Serialize(index);
    if (!bytes || bytes->size() > kIndexLimit) return Fail(error, "chunk index exceeds its limit");
    if (!storage_.Write(slot_ + "/index.kbsave", { reinterpret_cast<const char*>(bytes->data()), bytes->size() }))
        return Fail(error, "chunk index publication failed; reload the index before retrying");
    return true;
}

bool SaveGameChunkStore::Prune(std::string* error) {
    if (error != nullptr) error->clear();
    const auto snapshot = Open();
    if (!snapshot.succeeded) return Fail(error, snapshot.diagnostic);
    std::set<std::string> referenced;
    for (const auto& [name, hash] : snapshot.chunks) referenced.insert(ChunkPath(slot_, hash));
    const auto prefix = slot_ + "/chunks/";
    for (const auto& path : storage_.List()) {
        if (path == slot_ + "/index.kbsave.tmp") {
            if (!storage_.Delete(path)) return Fail(error, "stale index temporary file could not be removed");
            continue;
        }
        if (!path.starts_with(prefix)) continue;
        const bool temporary = path.ends_with(".kbsave.tmp");
        if (!temporary && !path.ends_with(".kbsave")) continue;
        const auto suffixBytes = temporary ? 11U : 7U;
        const auto hash = std::string_view{ path }.substr(prefix.size(), path.size() - prefix.size() - suffixBytes);
        if (HashName(hash) && (temporary || !referenced.contains(path)) && !storage_.Delete(path))
            return Fail(error, "unused chunk could not be removed");
    }
    return true;
}
} // namespace kb::save
