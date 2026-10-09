#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using namespace kb::ecs;
constexpr ComponentId kPayload = 20001U;
constexpr ComponentId kMarker = 20002U;
struct Payload { std::array<std::uint64_t, 8U> words{}; };
constexpr NativeComponentType kPayloadType{ kPayload, sizeof(Payload), alignof(Payload) };
constexpr NativeComponentType kMarkerType{ kMarker, sizeof(std::uint64_t), alignof(std::uint64_t) };

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Payload Value(std::size_t index, std::uint64_t seed) {
    Payload result{};
    for (std::size_t word = 0U; word < result.words.size(); ++word)
        result.words[word] = seed ^ (0x9E3779B97F4A7C15ULL * (index + 1U)) ^ (0xD1B54A32D192ED03ULL * (word + 1U));
    return result;
}

struct Fixture {
    WorldConfig config{ .chunkSizeProfile = ChunkSizeProfile::Chunk4KB };
    NativeArchetypeStorage native{ config };
    std::unordered_map<Entity::IdType, Payload> expected;
    std::size_t capacity = EstimateNativeArchetypeCapacity(std::span{ &kPayloadType, 1U }, config.chunkSizeProfile).entitiesPerChunk;

    std::vector<Entity> Spawn(std::size_t count, std::uint64_t seed) {
        std::vector<Payload> values(count);
        for (std::size_t index = 0U; index < count; ++index) values[index] = Value(index, seed);
        const NativeBulkComponentColumn column{ kPayloadType, values.data(), sizeof(Payload), values.size() };
        auto ids = native.CreateEntities(count, std::span{ &column, 1U });
        Require(ids.size() == values.size(), "Bulk create returned wrong ID count");
        for (std::size_t index = 0U; index < count; ++index) {
            Require(expected.emplace(ids[index].Id(), values[index]).second, "Bulk create duplicated a full entity ID");
        }
        return ids;
    }

    void Check(bool denseSingleArchetype = true) {
        const std::array terms{ kPayload };
        std::vector<QueryTableDispatchRecord> records;
        native.CollectQueryRecords(terms, terms, {}, records);
        std::unordered_map<Entity::IdType, unsigned> visits;
        std::size_t total = 0U;
        for (const auto& record : records) {
            Require(record.entityIds != nullptr && record.fieldComponents[0] != nullptr && record.entityCount != 0U,
                "Nonempty query record lost IDs or payload binding");
            if (denseSingleArchetype) {
                Require(record.nativeChunkIndex == total / capacity, "Live rows are not in dense chunk-prefix order");
                Require(record.entityCount == std::min(capacity, expected.size() - total),
                    "Retained append left a partial chunk before another live chunk");
            }
            const auto* values = static_cast<const Payload*>(record.fieldComponents[0]);
            for (std::size_t row = 0U; row < record.entityCount; ++row) {
                const Entity entity{ record.entityIds[row] };
                const auto oracle = expected.find(entity.Id());
                Require(oracle != expected.end() && ++visits[entity.Id()] == 1U, "Query exposed stale/unexpected/duplicate full ID");
                Require(native.IsAlive(entity), "Query full ID is not alive");
                Require(std::memcmp(&values[row], &oracle->second, sizeof(Payload)) == 0, "Query payload differs from owner oracle");
                const auto* direct = static_cast<const Payload*>(native.ComponentData(entity, kPayload));
                Require(std::memcmp(direct, &oracle->second, sizeof(Payload)) == 0, "Per-entity record location points at wrong payload");
                NativeComponentRows location{};
                const auto* pointer = native.TryGetMutableComponentRow(entity, kPayload, location);
                Require(pointer == direct && location.archetypeIndex == record.nativeArchetypeIndex
                    && location.chunkIndex == record.nativeChunkIndex && location.firstRow == row && location.count == 1U,
                    "Public wide row descriptor disagrees with ID/payload binding");
                Require(record.componentVersions[0] == native.ComponentVersion(entity, kPayload), "Query version snapshot disagrees with owner archetype");
            }
            total += record.entityCount;
        }
        Require(total == expected.size() && visits.size() == expected.size(), "Query coverage differs from live owner ledger");
        Require(native.Stats().liveEntities == expected.size(), "Native live count differs from owner ledger");
    }

    std::vector<Entity> WarmClearAndSmall() {
        const auto warm = Spawn(3U * capacity + 17U, 11U);
        Check();
        const auto chunks = native.ChunkCount();
        native.ClearRetainingCapacity();
        expected.clear();
        Require(native.ChunkCount() == chunks, "ClearRetainingCapacity released physical chunks");
        for (Entity stale : warm) Require(!native.IsAlive(stale), "Retained clear left an old full ID alive");
        auto small = Spawn(1U, 37U);
        Check();
        return small;
    }
};

void Run(int selector) {
    Fixture fixture;
    auto ids = fixture.WarmClearAndSmall();
    std::printf("START retained selector=%d capacity=%zu chunks=%zu first=%llu\n", selector,
        fixture.capacity, fixture.native.ChunkCount(), static_cast<unsigned long long>(ids[0].Id()));
    std::fflush(stdout);
    if (selector == 0) {
        fixture.native.DestroyEntity(ids[0]);
        fixture.expected.erase(ids[0].Id());
        fixture.Check();
    } else if (selector == 1) {
        const std::uint64_t marker = 123456789U;
        const NativeComponentValue value{ kMarkerType, &marker };
        fixture.native.AddComponents(ids[0], std::span{ &value, 1U });
        fixture.Check(false);
        Require(*static_cast<const std::uint64_t*>(fixture.native.ComponentData(ids[0], kMarker)) == marker,
            "Single migration lost added payload");
        const std::array removed{ kMarker };
        fixture.native.RemoveComponents(ids[0], removed);
        fixture.Check();
    } else if (selector == 2 || selector == 3) {
        if (selector == 2) {
            const Payload payload = Value(0U, 71U);
            const NativeComponentValue value{ kPayloadType, &payload };
            const Entity single = fixture.native.CreateEntity(std::span{ &value, 1U });
            fixture.expected.emplace(single.Id(), payload);
            ids.push_back(single);
            fixture.Check();
        }
        const auto appended = fixture.Spawn(fixture.capacity + 7U, 97U);
        ids.insert(ids.end(), appended.begin(), appended.end());
        fixture.Check();
        std::vector<Entity> removed;
        for (std::size_t index = 0U; index < ids.size(); index += 3U) removed.push_back(ids[index]);
        fixture.native.DestroyEntities(removed);
        for (Entity entity : removed) fixture.expected.erase(entity.Id());
        fixture.Check();
        const Entity survivor{ fixture.expected.begin()->first };
        fixture.native.DestroyEntity(survivor);
        fixture.expected.erase(survivor.Id());
        fixture.Check();
    } else if (selector == 4) {
        const auto appended = fixture.Spawn(fixture.capacity + 7U, 101U);
        ids.insert(ids.end(), appended.begin(), appended.end());
        fixture.Check();
        const std::uint64_t marker = 987654321U;
        const NativeBulkComponentColumn value{ kMarkerType, &marker, 0U, 1U };
        fixture.native.AddComponents(ids, std::span{ &value, 1U });
        fixture.Check(false);
        for (Entity entity : ids) Require(*static_cast<const std::uint64_t*>(fixture.native.ComponentData(entity, kMarker)) == marker,
            "Bulk retained migration lost row/owner payload");
        const std::array removed{ kMarker };
        fixture.native.RemoveComponents(ids, removed);
        fixture.Check();
    } else {
        throw std::invalid_argument("Retained-prefix selector must be 0..4");
    }
    std::printf("PASS retained selector=%d live=%zu chunks=%zu recordBytes=%zu\n", selector,
        fixture.expected.size(), fixture.native.ChunkCount(), fixture.native.Stats().entityRecordBytes);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const int selector = argc == 2 ? std::atoi(argv[1]) : -1;
        if (selector < 0) for (int index = 0; index <= 4; ++index) Run(index);
        else Run(selector);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL retained prefix: %s\n", error.what());
        return 1;
    }
}
