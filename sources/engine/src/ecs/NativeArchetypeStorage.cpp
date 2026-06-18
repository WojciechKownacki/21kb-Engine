#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kb::ecs {
namespace {

constexpr std::size_t kChunkAlignment = 64;
constexpr std::uint32_t kInvalidEntityIndex = std::numeric_limits<std::uint32_t>::max();

static_assert(sizeof(Entity) == sizeof(Entity::IdType), "Native ECS query batches require Entity to store exactly one id");
static_assert(alignof(Entity) == alignof(Entity::IdType), "Native ECS query batches require Entity id-compatible alignment");

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] bool IsAlignedAddress(const void* pointer, std::size_t alignment) noexcept {
    return pointer != nullptr && alignment != 0U && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

[[nodiscard]] const ComponentTypeInfo* FindComponentType(std::span<const ComponentTypeInfo> componentTypes, ComponentId componentId) noexcept {
    for (const ComponentTypeInfo& componentType : componentTypes) {
        if (componentType.id == componentId) {
            return &componentType;
        }
    }
    return nullptr;
}

struct ChunkKey {
    std::size_t archetypeIndex = 0;
    std::size_t chunkIndex = 0;

    [[nodiscard]] bool operator==(const ChunkKey& other) const noexcept {
        return archetypeIndex == other.archetypeIndex && chunkIndex == other.chunkIndex;
    }
};

struct ChunkKeyHash {
    [[nodiscard]] std::size_t operator()(const ChunkKey& key) const noexcept {
        std::size_t hash = key.archetypeIndex + 0x9E3779B97F4A7C15ULL;
        hash ^= key.chunkIndex + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

[[nodiscard]] const ChunkedComponentSnapshot* FindSnapshotComponent(
    const ChunkedWorldSnapshotChunk& chunk,
    ComponentId componentId) noexcept {
    for (const ChunkedComponentSnapshot& component : chunk.components) {
        if (component.componentId == componentId) {
            return &component;
        }
    }
    return nullptr;
}

[[nodiscard]] bool SameEntityIds(
    const ChunkedWorldSnapshotChunk& baseline,
    const Entity::IdType* entityIds,
    std::size_t rowCount) noexcept {
    if (baseline.entityIds.size() != rowCount) {
        return false;
    }
    for (std::size_t index = 0; index < rowCount; ++index) {
        if (baseline.entityIds[index] != entityIds[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameComponentSet(const ChunkedWorldSnapshotChunk& baseline, std::span<const NativeComponentType> types) noexcept {
    if (baseline.components.size() != types.size()) {
        return false;
    }
    for (const NativeComponentType& type : types) {
        if (FindSnapshotComponent(baseline, type.id) == nullptr) {
            return false;
        }
    }
    return true;
}

void AssertComponentAlignment(const void* pointer, const NativeComponentType& type) noexcept {
    static_cast<void>(pointer);
    static_cast<void>(type);
    assert(IsAlignedAddress(pointer, type.alignment) && "Native ECS component column violated declared alignment");
}

[[nodiscard]] Entity PackEntity(std::uint32_t index, std::uint32_t generation) noexcept {
    return Entity{ (static_cast<Entity::IdType>(generation) << 32U) | (static_cast<Entity::IdType>(index) + kGeneratedEntityIndexBase) };
}

[[nodiscard]] std::uint32_t EntityIndex(Entity entity) noexcept {
    const std::uint32_t index = GeneratedEntityIndex(entity);
    return index == kInvalidGeneratedEntityIndex ? kInvalidEntityIndex : index;
}

[[nodiscard]] std::uint32_t EntityGeneration(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity.Id() >> 32U);
}

[[nodiscard]] Entity::IdType StripEntityGeneration(Entity entity) noexcept {
    return entity.Id() & 0xFFFFFFFFULL;
}

void ValidateComponentType(const NativeComponentType& type) {
    if (type.id == 0 || type.size == 0 || type.alignment == 0 || !std::has_single_bit(type.alignment)) {
        throw std::invalid_argument("Invalid native ECS component type");
    }
    if (type.alignment > kChunkAlignment) {
        throw std::invalid_argument("Native ECS component alignment exceeds chunk alignment");
    }
    if ((type.size % type.alignment) != 0U) {
        throw std::invalid_argument("Native ECS component size must preserve row alignment");
    }
}

struct ComponentLayout {
    NativeComponentType type{};
    std::size_t offset = 0;
};

struct ArchetypeLayout {
    std::vector<ComponentLayout> columns;
    std::size_t capacity = 0;
    std::size_t bytesPerEntity = 0;
    std::size_t usedPayloadBytes = 0;
};

[[nodiscard]] std::size_t PayloadBytesForCapacity(std::span<const NativeComponentType> types, std::size_t capacity) {
    std::size_t offset = 0;
    for (const NativeComponentType& type : types) {
        offset = AlignUp(offset, type.alignment);
        offset += type.size * capacity;
    }
    return offset;
}

[[nodiscard]] ArchetypeLayout BuildLayout(std::span<const NativeComponentType> types, std::size_t payloadBytes) {
    ArchetypeLayout layout;
    layout.bytesPerEntity = 0;
    for (const NativeComponentType& type : types) {
        layout.bytesPerEntity += type.size;
    }
    if (types.empty()) {
        layout.capacity = std::max<std::size_t>(payloadBytes / sizeof(Entity), 1U);
        return layout;
    }

    std::size_t low = 0;
    std::size_t high = std::max<std::size_t>(1, payloadBytes / std::max<std::size_t>(layout.bytesPerEntity, 1));
    while (PayloadBytesForCapacity(types, high) <= payloadBytes && high < (std::numeric_limits<std::size_t>::max() / 2U)) {
        high *= 2U;
    }

    while (low < high) {
        const std::size_t mid = low + ((high - low + 1U) / 2U);
        if (PayloadBytesForCapacity(types, mid) <= payloadBytes) {
            low = mid;
        } else {
            high = mid - 1U;
        }
    }

    if (!types.empty() && low == 0) {
        throw std::invalid_argument("Native ECS chunk payload is too small for archetype");
    }

    layout.capacity = types.empty() ? std::max<std::size_t>(payloadBytes / sizeof(Entity), 1U) : low;
    std::size_t offset = 0;
    for (const NativeComponentType& type : types) {
        offset = AlignUp(offset, type.alignment);
        layout.columns.push_back(ComponentLayout{ .type = type, .offset = offset });
        offset += type.size * layout.capacity;
    }
    layout.usedPayloadBytes = offset;
    return layout;
}

class NativeChunkPool {
public:
    NativeChunkPool(std::size_t payloadBytes, std::size_t alignment)
        : payloadBytes_(payloadBytes)
        , alignment_(alignment) {}

    ~NativeChunkPool() {
        for (std::byte* block : freeList_) {
            ::operator delete(block, std::align_val_t{ alignment_ });
        }
    }

    NativeChunkPool(const NativeChunkPool&) = delete;
    NativeChunkPool& operator=(const NativeChunkPool&) = delete;

    [[nodiscard]] std::byte* Acquire() {
        ++acquireCount_;
        if (!freeList_.empty()) {
            std::byte* block = freeList_.back();
            freeList_.pop_back();
            ++chunksInUse_;
            ++reuseCount_;
            return block;
        }
        ++chunksInUse_;
        ++allocatedChunks_;
        return static_cast<std::byte*>(::operator new(payloadBytes_, std::align_val_t{ alignment_ }));
    }

    void Release(std::byte* block) noexcept {
        if (block == nullptr) {
            return;
        }
#if !defined(NDEBUG)
        std::memset(block, 0xDD, payloadBytes_);
#endif
        --chunksInUse_;
        ++releaseCount_;
        freeList_.push_back(block);
    }

    [[nodiscard]] std::size_t TrimFreeChunks(std::size_t maxFreeChunksToKeep, std::size_t maxChunksToRelease) noexcept {
        const std::size_t releasable = freeList_.size() > maxFreeChunksToKeep ? freeList_.size() - maxFreeChunksToKeep : 0U;
        const std::size_t releaseCount = std::min(releasable, maxChunksToRelease);
        for (std::size_t index = 0; index < releaseCount; ++index) {
            std::byte* block = freeList_.back();
            freeList_.pop_back();
            ::operator delete(block, std::align_val_t{ alignment_ });
            --allocatedChunks_;
            ++trimCount_;
        }
        return releaseCount;
    }

    [[nodiscard]] std::size_t PayloadBytes() const noexcept { return payloadBytes_; }
    [[nodiscard]] std::size_t ChunksInUse() const noexcept { return chunksInUse_; }
    [[nodiscard]] std::size_t AllocatedChunks() const noexcept { return allocatedChunks_; }
    [[nodiscard]] std::size_t FreeChunks() const noexcept { return freeList_.size(); }
    [[nodiscard]] std::size_t AcquireCount() const noexcept { return acquireCount_; }
    [[nodiscard]] std::size_t ReuseCount() const noexcept { return reuseCount_; }
    [[nodiscard]] std::size_t ReleaseCount() const noexcept { return releaseCount_; }
    [[nodiscard]] std::size_t TrimCount() const noexcept { return trimCount_; }

private:
    std::size_t payloadBytes_ = 0;
    std::size_t alignment_ = kChunkAlignment;
    std::size_t chunksInUse_ = 0;
    std::size_t allocatedChunks_ = 0;
    std::size_t acquireCount_ = 0;
    std::size_t reuseCount_ = 0;
    std::size_t releaseCount_ = 0;
    std::size_t trimCount_ = 0;
    std::vector<std::byte*> freeList_;
};

class NativeChunkBuffer {
public:
    NativeChunkBuffer() noexcept = default;

    explicit NativeChunkBuffer(NativeChunkPool& pool)
        : pool_(&pool)
        , data_(pool.Acquire()) {}

    ~NativeChunkBuffer() {
        Reset();
    }

    NativeChunkBuffer(const NativeChunkBuffer&) = delete;
    NativeChunkBuffer& operator=(const NativeChunkBuffer&) = delete;

    NativeChunkBuffer(NativeChunkBuffer&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr))
        , data_(std::exchange(other.data_, nullptr)) {}

    NativeChunkBuffer& operator=(NativeChunkBuffer&& other) noexcept {
        if (this != &other) {
            Reset();
            pool_ = std::exchange(other.pool_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] std::byte* Data() noexcept { return data_; }
    [[nodiscard]] const std::byte* Data() const noexcept { return data_; }

private:
    void Reset() noexcept {
        if (pool_ != nullptr) {
            pool_->Release(data_);
            pool_ = nullptr;
            data_ = nullptr;
        }
    }

    NativeChunkPool* pool_ = nullptr;
    std::byte* data_ = nullptr;
};

struct NativeChunk {
    NativeChunkBuffer payload;
    std::vector<Entity> entities;
    std::size_t rowCount = 0;

    NativeChunk(NativeChunkPool& pool, std::size_t capacity)
        : payload(pool)
        , entities(capacity) {}

    NativeChunk(const NativeChunk&) = delete;
    NativeChunk& operator=(const NativeChunk&) = delete;
    NativeChunk(NativeChunk&&) noexcept = default;
    NativeChunk& operator=(NativeChunk&&) noexcept = default;
};

struct EntityLocation {
    std::size_t table = 0;
    std::size_t chunk = 0;
    std::size_t row = 0;
};

struct EntityRecord {
    std::uint32_t generation = 0;
    bool alive = false;
    bool ownsGeneratedId = true;
    Entity entity{};
    EntityLocation location{};
};

enum class EdgeKind : std::uint8_t {
    Add,
    Remove,
};

struct EdgeKey {
    EdgeKind kind = EdgeKind::Add;
    std::vector<ComponentId> componentIds;

    [[nodiscard]] bool operator==(const EdgeKey& other) const noexcept {
        return kind == other.kind && componentIds == other.componentIds;
    }
};

struct EdgeKeyHash {
    [[nodiscard]] std::size_t operator()(const EdgeKey& key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.kind) + 0x9E3779B97F4A7C15ULL;
        for (ComponentId id : key.componentIds) {
            hash ^= std::hash<ComponentId>{}(id) + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
        }
        return hash;
    }
};

class ComponentSignature {
public:
    void Set(std::size_t bitIndex) {
        const std::size_t block = bitIndex / kBitsPerBlock;
        if (block >= blocks_.size()) {
            throw std::out_of_range("Native ECS component signature capacity exceeded");
        }
        blocks_[block] |= std::uint64_t{ 1 } << (bitIndex % kBitsPerBlock);
        blockCount_ = std::max(blockCount_, block + 1U);
    }

    [[nodiscard]] bool Has(std::size_t bitIndex) const noexcept {
        const std::size_t block = bitIndex / kBitsPerBlock;
        return block < blockCount_ && (blocks_[block] & (std::uint64_t{ 1 } << (bitIndex % kBitsPerBlock))) != 0;
    }

    [[nodiscard]] bool ContainsAll(const ComponentSignature& required) const noexcept {
        for (std::size_t index = 0; index < required.blockCount_; ++index) {
            const std::uint64_t available = index < blockCount_ ? blocks_[index] : 0U;
            if ((available & required.blocks_[index]) != required.blocks_[index]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool operator==(const ComponentSignature& other) const noexcept {
        if (blockCount_ != other.blockCount_) {
            return false;
        }
        for (std::size_t index = 0; index < blockCount_; ++index) {
            if (blocks_[index] != other.blocks_[index]) {
                return false;
            }
        }
        return true;
    }

private:
    static constexpr std::size_t kBitsPerBlock = 64;
    static constexpr std::size_t kMaxBlocks = 64;

    std::array<std::uint64_t, kMaxBlocks> blocks_{};
    std::size_t blockCount_ = 0;
};

class ComponentSignatureRegistry {
public:
    [[nodiscard]] ComponentSignature Build(std::span<const NativeComponentType> types) {
        ComponentSignature signature;
        for (const NativeComponentType& type : types) {
            signature.Set(ResolveBit(type.id));
        }
        return signature;
    }

    [[nodiscard]] std::optional<ComponentSignature> TryBuild(std::span<const ComponentId> componentIds) const {
        ComponentSignature signature;
        for (ComponentId componentId : componentIds) {
            const auto found = componentBits_.find(componentId);
            if (found == componentBits_.end()) {
                return std::nullopt;
            }
            signature.Set(found->second);
        }
        return signature;
    }

    [[nodiscard]] std::optional<std::size_t> FindBit(ComponentId componentId) const noexcept {
        const auto found = componentBits_.find(componentId);
        if (found == componentBits_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    [[nodiscard]] std::size_t ResolveBit(ComponentId componentId) {
        const auto found = componentBits_.find(componentId);
        if (found != componentBits_.end()) {
            return found->second;
        }

        const std::size_t bitIndex = componentBits_.size();
        componentBits_.emplace(componentId, bitIndex);
        return bitIndex;
    }

    std::unordered_map<ComponentId, std::size_t> componentBits_;
};

class ArchetypeTable {
public:
    ArchetypeTable(NativeChunkPool& pool, std::vector<NativeComponentType> types, ComponentSignature signature)
        : pool_(&pool)
        , types_(std::move(types))
        , signature_(std::move(signature))
        , layout_(BuildLayout(types_, pool.PayloadBytes())) {}

    ArchetypeTable(const ArchetypeTable&) = delete;
    ArchetypeTable& operator=(const ArchetypeTable&) = delete;
    ArchetypeTable(ArchetypeTable&&) noexcept = default;
    ArchetypeTable& operator=(ArchetypeTable&&) noexcept = default;

    [[nodiscard]] std::span<const NativeComponentType> Types() const noexcept { return types_; }
    [[nodiscard]] const ComponentSignature& Signature() const noexcept { return signature_; }
    [[nodiscard]] std::size_t LiveEntities() const noexcept { return liveEntities_; }
    [[nodiscard]] std::size_t ChunkCount() const noexcept { return chunks_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return layout_.capacity; }
    [[nodiscard]] std::size_t UsedBytes() const noexcept { return liveEntities_ * layout_.bytesPerEntity; }
    [[nodiscard]] std::uint64_t Version() const noexcept { return version_; }

    [[nodiscard]] bool HasComponent(ComponentId componentId) const noexcept {
        return FindColumn(componentId) != nullptr;
    }

    [[nodiscard]] bool HasSignatureBit(std::size_t bitIndex) const noexcept {
        return signature_.Has(bitIndex);
    }

    [[nodiscard]] bool Matches(const ComponentSignature& required) const noexcept {
        return signature_.ContainsAll(required);
    }

    [[nodiscard]] std::uint64_t ComponentVersion(ComponentId componentId) const {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr) {
            throw std::out_of_range("Native ECS component is not in archetype");
        }
        const std::size_t index = static_cast<std::size_t>(column - layout_.columns.data());
        return componentVersions_[index];
    }

    [[nodiscard]] std::uint64_t ComponentVersionOrZero(ComponentId componentId) const noexcept {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr) {
            return 0;
        }
        const std::size_t index = static_cast<std::size_t>(column - layout_.columns.data());
        return index < componentVersions_.size() ? componentVersions_[index] : 0;
    }

    [[nodiscard]] const std::unordered_map<EdgeKey, std::size_t, EdgeKeyHash>& Edges() const noexcept {
        return edges_;
    }

    [[nodiscard]] std::unordered_map<EdgeKey, std::size_t, EdgeKeyHash>& Edges() noexcept {
        return edges_;
    }

    [[nodiscard]] EntityLocation Add(Entity entity) {
        if (chunks_.empty() || chunks_.back().rowCount == layout_.capacity) {
            chunks_.emplace_back(*pool_, layout_.capacity);
        }
        EntityLocation location{ .chunk = chunks_.size() - 1U, .row = chunks_.back().rowCount };
        chunks_.back().entities[location.row] = entity;
        ZeroRow(location);
        ++chunks_.back().rowCount;
        ++liveEntities_;
        ++version_;
        if (componentVersions_.empty()) {
            componentVersions_.resize(layout_.columns.size(), 1U);
        }
        return location;
    }

    void AddMany(std::span<const Entity> entities, std::size_t tableIndex, std::vector<EntityLocation>& locations, bool clearRows = true) {
        locations.clear();
        locations.reserve(entities.size());
        if (entities.empty()) {
            return;
        }
        if (componentVersions_.empty()) {
            componentVersions_.resize(layout_.columns.size(), 1U);
        }

        std::size_t consumed = 0;
        while (consumed < entities.size()) {
            if (chunks_.empty() || chunks_.back().rowCount == layout_.capacity) {
                chunks_.emplace_back(*pool_, layout_.capacity);
            }

            NativeChunk& chunk = chunks_.back();
            const std::size_t chunkIndex = chunks_.size() - 1U;
            const std::size_t firstRow = chunk.rowCount;
            const std::size_t writable = std::min(entities.size() - consumed, layout_.capacity - firstRow);
            std::memcpy(chunk.entities.data() + firstRow, entities.data() + consumed, writable * sizeof(Entity));

            if (clearRows) {
                for (const ComponentLayout& column : layout_.columns) {
                    std::memset(chunk.payload.Data() + column.offset + (firstRow * column.type.size), 0, writable * column.type.size);
                }
            }

            for (std::size_t offset = 0; offset < writable; ++offset) {
                locations.push_back(EntityLocation{ .table = tableIndex, .chunk = chunkIndex, .row = firstRow + offset });
            }

            chunk.rowCount += writable;
            liveEntities_ += writable;
            consumed += writable;
        }
        version_ += entities.size();
    }

    [[nodiscard]] Entity RemoveAt(EntityLocation location) {
        if (location.chunk >= chunks_.size() || location.row >= chunks_[location.chunk].rowCount) {
            throw std::out_of_range("Invalid native ECS row location");
        }

        NativeChunk& lastChunk = chunks_.back();
        EntityLocation last{ .chunk = chunks_.size() - 1U, .row = lastChunk.rowCount - 1U };
        Entity movedEntity{};
        if (location.chunk != last.chunk || location.row != last.row) {
            CopyRow(last, location);
            movedEntity = lastChunk.entities[last.row];
            chunks_[location.chunk].entities[location.row] = movedEntity;
        }

        --lastChunk.rowCount;
        --liveEntities_;
        ++version_;
        for (std::uint64_t& componentVersion : componentVersions_) {
            ++componentVersion;
        }

        if (lastChunk.rowCount == 0) {
            chunks_.pop_back();
        }
        return movedEntity;
    }

    void RemoveMany(
        std::span<const EntityLocation> locations,
        std::vector<std::pair<Entity, EntityLocation>>& movedEntities,
        std::vector<std::size_t>& removedRows) {
        movedEntities.clear();
        removedRows.clear();
        if (locations.empty()) {
            return;
        }

        removedRows.reserve(locations.size());
        for (EntityLocation location : locations) {
            if (location.chunk >= chunks_.size() || location.row >= chunks_[location.chunk].rowCount) {
                throw std::out_of_range("Invalid native ECS row location");
            }
            removedRows.push_back(FlatRow(location));
        }
        std::sort(removedRows.begin(), removedRows.end());
        if (std::adjacent_find(removedRows.begin(), removedRows.end()) != removedRows.end()) {
            throw std::invalid_argument("Native ECS bulk destroy received duplicate row locations");
        }

        const std::size_t originalLiveEntities = liveEntities_;
        const std::size_t targetLiveEntities = originalLiveEntities - removedRows.size();
        std::size_t tail = originalLiveEntities - 1U;
        for (std::size_t removedIndex = 0; removedIndex < removedRows.size(); ++removedIndex) {
            const std::size_t destinationRow = removedRows[removedIndex];
            if (destinationRow >= targetLiveEntities) {
                continue;
            }

            while (std::binary_search(removedRows.begin(), removedRows.end(), tail)) {
                --tail;
            }

            const EntityLocation source = LocationFromFlatRow(tail);
            const EntityLocation destination = LocationFromFlatRow(destinationRow);
            CopyRow(source, destination);
            Entity movedEntity = chunks_[source.chunk].entities[source.row];
            chunks_[destination.chunk].entities[destination.row] = movedEntity;
            movedEntities.push_back(std::pair{ movedEntity, destination });
            --tail;
        }

        ResizeRows(targetLiveEntities);
        liveEntities_ = targetLiveEntities;
        version_ += removedRows.size();
        for (std::uint64_t& componentVersion : componentVersions_) {
            componentVersion += removedRows.size();
        }
    }

    void CopyEntityTo(EntityLocation source, ArchetypeTable& target, EntityLocation targetLocation) const {
        if (source.chunk >= chunks_.size() || source.row >= chunks_[source.chunk].rowCount) {
            throw std::out_of_range("Invalid native ECS source row location");
        }

        for (const NativeComponentType& type : types_) {
            if (target.HasComponent(type.id)) {
                std::memcpy(target.ComponentData(targetLocation, type.id), ComponentData(source, type.id), type.size);
                target.TouchComponent(type.id);
            }
        }
    }

    void CopyEntitiesTo(
        std::span<const EntityLocation> sources,
        ArchetypeTable& target,
        std::span<const EntityLocation> targets) const {
        if (sources.size() != targets.size()) {
            throw std::invalid_argument("Native ECS bulk migration requires matching source and target row counts");
        }

        for (const NativeComponentType& type : types_) {
            if (!target.HasComponent(type.id)) {
                continue;
            }

            std::size_t consumed = 0;
            while (consumed < sources.size()) {
                const EntityLocation firstSource = sources[consumed];
                const EntityLocation firstTarget = targets[consumed];
                ValidateLocation(firstSource);

                std::size_t count = 1U;
                while (consumed + count < sources.size()) {
                    const EntityLocation nextSource = sources[consumed + count];
                    const EntityLocation nextTarget = targets[consumed + count];
                    if (nextSource.chunk != firstSource.chunk || nextTarget.chunk != firstTarget.chunk ||
                        nextSource.row != firstSource.row + count || nextTarget.row != firstTarget.row + count) {
                        break;
                    }
                    ValidateLocation(nextSource);
                    ++count;
                }

                const ComponentLayout* sourceColumn = FindColumn(type.id);
                const ComponentLayout* targetColumn = target.FindColumn(type.id);
                if (sourceColumn == nullptr || targetColumn == nullptr) {
                    throw std::out_of_range("Native ECS bulk migration component column is unavailable");
                }
                const auto* sourceData = chunks_[firstSource.chunk].payload.Data() + sourceColumn->offset + (firstSource.row * sourceColumn->type.size);
                auto* targetData = target.chunks_[firstTarget.chunk].payload.Data() + targetColumn->offset + (firstTarget.row * targetColumn->type.size);
                std::memcpy(targetData, sourceData, count * sourceColumn->type.size);
                target.TouchComponent(type.id);
                consumed += count;
            }
        }
    }

    void WriteComponent(EntityLocation location, ComponentId componentId, const void* data, std::size_t size) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || column->type.size != size || data == nullptr) {
            throw std::invalid_argument("Invalid native ECS component write");
        }
        std::memcpy(ComponentData(location, componentId), data, size);
        TouchComponent(componentId);
    }

    void WriteComponentColumn(
        std::size_t chunkIndex,
        std::size_t firstRow,
        ComponentId componentId,
        const void* data,
        std::size_t count,
        std::size_t stride) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || data == nullptr || count == 0U) {
            throw std::invalid_argument("Invalid native ECS component column write");
        }
        if (chunkIndex >= chunks_.size() || firstRow + count > chunks_[chunkIndex].rowCount) {
            throw std::out_of_range("Invalid native ECS component column range");
        }

        auto* destination = chunks_[chunkIndex].payload.Data() + column->offset + (firstRow * column->type.size);
        const auto* source = static_cast<const std::byte*>(data);
        const bool broadcast = stride == 0U;
        const std::size_t sourceStride = broadcast ? 0U : stride;
        if (!broadcast && sourceStride < column->type.size) {
            throw std::invalid_argument("Invalid native ECS component column stride");
        }
        if (broadcast) {
            WriteRepeatedComponentRows(destination, source, column->type.size, count);
        } else if (sourceStride == column->type.size) {
            std::memcpy(destination, source, count * column->type.size);
        } else {
            for (std::size_t row = 0; row < count; ++row) {
                std::memcpy(destination + (row * column->type.size), source + (row * sourceStride), column->type.size);
            }
        }
        TouchComponent(componentId);
    }

    static void WriteRepeatedComponentRows(std::byte* destination, const std::byte* source, std::size_t componentSize, std::size_t rowCount) {
        if (destination == nullptr || source == nullptr || componentSize == 0U || rowCount == 0U) {
            return;
        }

        std::memcpy(destination, source, componentSize);
        std::size_t filledRows = 1U;
        while (filledRows < rowCount) {
            const std::size_t copyRows = std::min(filledRows, rowCount - filledRows);
            std::memcpy(destination + (filledRows * componentSize), destination, copyRows * componentSize);
            filledRows += copyRows;
        }
    }

    void WriteComponentColumnPattern(
        std::size_t chunkIndex,
        std::size_t firstRow,
        ComponentId componentId,
        const void* data,
        std::size_t count,
        std::size_t stride,
        std::size_t sourceCount,
        std::size_t sourceOffset) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || data == nullptr || count == 0U || sourceCount == 0U) {
            throw std::invalid_argument("Invalid native ECS component pattern write");
        }
        if (chunkIndex >= chunks_.size() || firstRow + count > chunks_[chunkIndex].rowCount) {
            throw std::out_of_range("Invalid native ECS component pattern range");
        }
        if (stride < column->type.size) {
            throw std::invalid_argument("Invalid native ECS component pattern stride");
        }

        auto* destination = chunks_[chunkIndex].payload.Data() + column->offset + (firstRow * column->type.size);
        const auto* source = static_cast<const std::byte*>(data);
        std::size_t written = 0;
        std::size_t sourceIndex = sourceOffset % sourceCount;
        while (written < count) {
            const std::size_t run = std::min(sourceCount - sourceIndex, count - written);
            if (stride == column->type.size) {
                std::memcpy(destination + (written * column->type.size), source + (sourceIndex * stride), run * column->type.size);
            } else {
                for (std::size_t row = 0; row < run; ++row) {
                    std::memcpy(
                        destination + ((written + row) * column->type.size),
                        source + ((sourceIndex + row) * stride),
                        column->type.size);
                }
            }
            written += run;
            sourceIndex = 0;
        }
        TouchComponent(componentId);
    }

    void WriteComponentRows(
        std::span<const EntityLocation> locations,
        ComponentId componentId,
        const void* data,
        std::span<const std::size_t> sourceRows,
        std::size_t stride) {
        if (locations.size() != sourceRows.size()) {
            throw std::invalid_argument("Native ECS component row write requires matching location and source row counts");
        }
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || data == nullptr) {
            throw std::invalid_argument("Invalid native ECS component row write");
        }
        const bool broadcast = stride == 0U;
        const std::size_t sourceStride = broadcast ? 0U : stride;
        if (!broadcast && sourceStride < column->type.size) {
            throw std::invalid_argument("Invalid native ECS component row stride");
        }

        const auto* source = static_cast<const std::byte*>(data);
        std::size_t consumed = 0;
        while (consumed < locations.size()) {
            const EntityLocation first = locations[consumed];
            ValidateLocation(first);
            std::size_t count = 1U;
            while (consumed + count < locations.size()) {
                const EntityLocation next = locations[consumed + count];
                if (next.chunk != first.chunk || next.row != first.row + count || sourceRows[consumed + count] != sourceRows[consumed] + count) {
                    break;
                }
                ValidateLocation(next);
                ++count;
            }

            auto* destination = chunks_[first.chunk].payload.Data() + column->offset + (first.row * column->type.size);
            const auto* sourceData = broadcast ? source : source + (sourceRows[consumed] * sourceStride);
            if (broadcast) {
                for (std::size_t index = 0; index < count; ++index) {
                    std::memcpy(destination + (index * column->type.size), sourceData, column->type.size);
                }
            } else if (sourceStride == column->type.size) {
                std::memcpy(destination, sourceData, count * column->type.size);
            } else {
                for (std::size_t index = 0; index < count; ++index) {
                    std::memcpy(destination + (index * column->type.size), sourceData + (index * sourceStride), column->type.size);
                }
            }
            TouchComponent(componentId);
            consumed += count;
        }
    }

    void MarkComponentsModified(std::span<const ComponentId> componentIds) {
        for (ComponentId componentId : componentIds) {
            TouchComponent(componentId);
        }
    }

    [[nodiscard]] void* ComponentData(EntityLocation location, ComponentId componentId) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr) {
            throw std::out_of_range("Native ECS component is not in archetype");
        }
        return ComponentData(location, *column);
    }

    [[nodiscard]] const Entity::IdType* ChunkEntityIds(std::size_t chunkIndex) const {
        if (chunkIndex >= chunks_.size()) {
            throw std::out_of_range("Invalid native ECS chunk index");
        }
        return reinterpret_cast<const Entity::IdType*>(chunks_[chunkIndex].entities.data());
    }

    [[nodiscard]] std::size_t ChunkRowCount(std::size_t chunkIndex) const {
        if (chunkIndex >= chunks_.size()) {
            throw std::out_of_range("Invalid native ECS chunk index");
        }
        return chunks_[chunkIndex].rowCount;
    }

    [[nodiscard]] std::size_t ChunkUsedBytes(std::size_t chunkIndex) const {
        return ChunkRowCount(chunkIndex) * layout_.bytesPerEntity;
    }

    [[nodiscard]] const void* ComponentColumnData(std::size_t chunkIndex, ComponentId componentId) const {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || chunkIndex >= chunks_.size()) {
            throw std::out_of_range("Native ECS component column is not available");
        }
        return chunks_[chunkIndex].payload.Data() + column->offset;
    }

    [[nodiscard]] void* MutableComponentColumnData(std::size_t chunkIndex, ComponentId componentId) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || chunkIndex >= chunks_.size()) {
            throw std::out_of_range("Native ECS component column is not available");
        }
        return chunks_[chunkIndex].payload.Data() + column->offset;
    }

    [[nodiscard]] const void* ComponentData(EntityLocation location, ComponentId componentId) const {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr) {
            throw std::out_of_range("Native ECS component is not in archetype");
        }
        return ComponentData(location, *column);
    }

private:
    void ValidateLocation(EntityLocation location) const {
        if (location.chunk >= chunks_.size() || location.row >= chunks_[location.chunk].rowCount) {
            throw std::out_of_range("Invalid native ECS row location");
        }
    }

    [[nodiscard]] const ComponentLayout* FindColumn(ComponentId componentId) const noexcept {
        const auto match = std::lower_bound(
            layout_.columns.begin(),
            layout_.columns.end(),
            componentId,
            [](const ComponentLayout& column, ComponentId id) {
                return column.type.id < id;
            });
        return match != layout_.columns.end() && match->type.id == componentId ? &*match : nullptr;
    }

    void TouchComponent(ComponentId componentId) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr) {
            throw std::out_of_range("Native ECS component is not in archetype");
        }
        const std::size_t index = static_cast<std::size_t>(column - layout_.columns.data());
        ++componentVersions_[index];
        ++version_;
    }

    [[nodiscard]] void* ComponentData(EntityLocation location, const ComponentLayout& column) {
        void* data = chunks_[location.chunk].payload.Data() + column.offset + (location.row * column.type.size);
        AssertComponentAlignment(data, column.type);
        return data;
    }

    [[nodiscard]] const void* ComponentData(EntityLocation location, const ComponentLayout& column) const {
        const void* data = chunks_[location.chunk].payload.Data() + column.offset + (location.row * column.type.size);
        AssertComponentAlignment(data, column.type);
        return data;
    }

    void CopyRow(EntityLocation source, EntityLocation target) {
        for (const ComponentLayout& column : layout_.columns) {
            std::memcpy(ComponentData(target, column), ComponentData(source, column), column.type.size);
        }
    }

    void ZeroRow(EntityLocation location) {
        for (const ComponentLayout& column : layout_.columns) {
            std::memset(ComponentData(location, column), 0, column.type.size);
        }
    }

    [[nodiscard]] std::size_t FlatRow(EntityLocation location) const noexcept {
        return location.chunk * layout_.capacity + location.row;
    }

    [[nodiscard]] EntityLocation LocationFromFlatRow(std::size_t flatRow) const noexcept {
        return EntityLocation{
            .chunk = flatRow / layout_.capacity,
            .row = flatRow % layout_.capacity,
        };
    }

    void ResizeRows(std::size_t targetLiveEntities) {
        if (targetLiveEntities == 0U) {
            chunks_.clear();
            return;
        }

        const std::size_t requiredChunks = ((targetLiveEntities - 1U) / layout_.capacity) + 1U;
        while (chunks_.size() > requiredChunks) {
            chunks_.pop_back();
        }
        for (std::size_t chunkIndex = 0; chunkIndex < chunks_.size(); ++chunkIndex) {
            const std::size_t firstRow = chunkIndex * layout_.capacity;
            chunks_[chunkIndex].rowCount = std::min(layout_.capacity, targetLiveEntities - firstRow);
        }
    }

    NativeChunkPool* pool_ = nullptr;
    std::vector<NativeComponentType> types_;
    ComponentSignature signature_;
    ArchetypeLayout layout_;
    std::vector<NativeChunk> chunks_;
    std::vector<std::uint64_t> componentVersions_;
    std::unordered_map<EdgeKey, std::size_t, EdgeKeyHash> edges_;
    std::size_t liveEntities_ = 0;
    std::uint64_t version_ = 1;
};

[[nodiscard]] std::vector<NativeComponentType> NormalizeTypes(std::span<const NativeComponentValue> components) {
    std::vector<NativeComponentType> types;
    types.reserve(components.size());
    for (const NativeComponentValue& component : components) {
        ValidateComponentType(component.type);
        types.push_back(component.type);
    }
    std::sort(types.begin(), types.end(), [](const NativeComponentType& lhs, const NativeComponentType& rhs) {
        return lhs.id < rhs.id;
    });
    const auto duplicate = std::adjacent_find(types.begin(), types.end(), [](const NativeComponentType& lhs, const NativeComponentType& rhs) {
        return lhs.id == rhs.id;
    });
    if (duplicate != types.end()) {
        throw std::invalid_argument("Native ECS archetype contains duplicate component types");
    }
    return types;
}

[[nodiscard]] std::vector<NativeComponentType> NormalizeTypes(std::span<const NativeBulkComponentColumn> components) {
    std::vector<NativeComponentType> types;
    types.reserve(components.size());
    for (const NativeBulkComponentColumn& component : components) {
        ValidateComponentType(component.type);
        if (component.data == nullptr) {
            throw std::invalid_argument("Native ECS bulk component column is missing data");
        }
        if (component.stride != 0U && component.stride < component.type.size) {
            throw std::invalid_argument("Native ECS bulk component column stride is too small");
        }
        types.push_back(component.type);
    }
    std::sort(types.begin(), types.end(), [](const NativeComponentType& lhs, const NativeComponentType& rhs) {
        return lhs.id < rhs.id;
    });
    const auto duplicate = std::adjacent_find(types.begin(), types.end(), [](const NativeComponentType& lhs, const NativeComponentType& rhs) {
        return lhs.id == rhs.id;
    });
    if (duplicate != types.end()) {
        throw std::invalid_argument("Native ECS archetype contains duplicate component types");
    }
    return types;
}

void ValidateAppendBulkColumnSourceCounts(std::span<const NativeBulkComponentColumn> components, std::size_t rowCount) {
    for (const NativeBulkComponentColumn& component : components) {
        const std::size_t sourceCount = component.sourceCount == 0U ? rowCount : component.sourceCount;
        if (sourceCount == 0U || sourceCount > rowCount || (rowCount % sourceCount) != 0U) {
            throw std::invalid_argument("Native ECS bulk append component source count must divide row count");
        }
    }
}

void ValidateRowMappedBulkColumnSourceCounts(std::span<const NativeBulkComponentColumn> components, std::size_t rowCount) {
    for (const NativeBulkComponentColumn& component : components) {
        const std::size_t sourceCount = component.sourceCount == 0U ? rowCount : component.sourceCount;
        if (sourceCount == 0U || sourceCount > rowCount || (sourceCount != 1U && sourceCount != rowCount)) {
            throw std::invalid_argument("Native ECS row-mapped bulk component source count must be broadcast or full row count");
        }
    }
}

[[nodiscard]] std::size_t ResolveRowMappedBulkColumnStride(const NativeBulkComponentColumn& component, std::size_t rowCount) {
    const std::size_t sourceCount = component.sourceCount == 0U ? rowCount : component.sourceCount;
    return sourceCount == 1U ? 0U : (component.stride == 0U ? component.type.size : component.stride);
}

[[nodiscard]] std::vector<ComponentId> NormalizeComponentIds(std::span<const ComponentId> componentIds) {
    std::vector<ComponentId> ids(componentIds.begin(), componentIds.end());
    std::sort(ids.begin(), ids.end());
    if (std::binary_search(ids.begin(), ids.end(), ComponentId{})) {
        throw std::invalid_argument("Native ECS component id must be non-zero");
    }
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        throw std::invalid_argument("Native ECS component id list contains duplicate ids");
    }
    return ids;
}

class StackComponentIdSet {
public:
    [[nodiscard]] bool Assign(std::span<const ComponentId> componentIds) {
        size_ = 0;
        return Append(componentIds) && SortAndRejectDuplicates();
    }

    [[nodiscard]] bool AssignUnion(std::span<const ComponentId> left, std::span<const ComponentId> right) {
        StackComponentIdSet leftSet;
        StackComponentIdSet rightSet;
        if (!leftSet.Assign(left) || !rightSet.Assign(right)) {
            return false;
        }

        size_ = 0;
        return Append(leftSet.Values()) && Append(rightSet.Values()) && SortAndDeduplicate();
    }

    [[nodiscard]] std::span<const ComponentId> Values() const noexcept {
        return std::span<const ComponentId>{ ids_.data(), size_ };
    }

private:
    [[nodiscard]] bool Append(std::span<const ComponentId> componentIds) {
        if (componentIds.size() > ids_.size() - size_) {
            return false;
        }
        for (ComponentId componentId : componentIds) {
            if (componentId == 0) {
                throw std::invalid_argument("Native ECS component id must be non-zero");
            }
            ids_[size_++] = componentId;
        }
        return true;
    }

    [[nodiscard]] bool SortAndRejectDuplicates() {
        std::sort(ids_.begin(), ids_.begin() + static_cast<std::ptrdiff_t>(size_));
        if (std::adjacent_find(ids_.begin(), ids_.begin() + static_cast<std::ptrdiff_t>(size_)) != ids_.begin() + static_cast<std::ptrdiff_t>(size_)) {
            throw std::invalid_argument("Native ECS component id list contains duplicate ids");
        }
        return true;
    }

    [[nodiscard]] bool SortAndDeduplicate() noexcept {
        std::sort(ids_.begin(), ids_.begin() + static_cast<std::ptrdiff_t>(size_));
        const auto uniqueEnd = std::unique(ids_.begin(), ids_.begin() + static_cast<std::ptrdiff_t>(size_));
        size_ = static_cast<std::size_t>(uniqueEnd - ids_.begin());
        return true;
    }

    std::array<ComponentId, kQueryExecutionScratchMaxTerms * 2U> ids_{};
    std::size_t size_ = 0;
};

[[nodiscard]] bool SameTypes(std::span<const NativeComponentType> lhs, std::span<const NativeComponentType> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].id != rhs[index].id || lhs[index].size != rhs[index].size || lhs[index].alignment != rhs[index].alignment) {
            return false;
        }
    }
    return true;
}

} // namespace

class NativeArchetypeStorage::Impl {
    struct BulkMigrationGroup {
        std::vector<Entity> entities;
        std::vector<EntityLocation> sourceLocations;
        std::vector<std::size_t> sourceRows;
        std::vector<std::uint32_t> recordIndices;
        std::size_t inputRowCount = 0;

        void Clear() noexcept {
            entities.clear();
            sourceLocations.clear();
            sourceRows.clear();
            recordIndices.clear();
            inputRowCount = 0;
        }
    };

public:
    explicit Impl(WorldConfig config)
        : chunkPayloadBytes_(kb::ecs::ChunkPayloadBytes(config.chunkSizeProfile))
        , pool_(chunkPayloadBytes_, kChunkAlignment) {
        if (chunkPayloadBytes_ == 0) {
            throw std::invalid_argument("Invalid native ECS chunk size profile");
        }
        records_.reserve(config.reserveEntities);
        freeEntityIndices_.reserve(config.reserveEntities);
        tables_.reserve(config.reserveArchetypes);
    }

    [[nodiscard]] Entity CreateEntity(std::span<const NativeComponentValue> components) {
        const std::vector<NativeComponentType> types = NormalizeTypes(components);
        const std::size_t tableIndex = FindOrCreateTable(types);
        ArchetypeTable& table = tables_[tableIndex];
        const Entity entity = AllocateEntity();
        EntityLocation location = table.Add(entity);
        location.table = tableIndex;
        records_[RecordIndex(entity)].location = location;

        for (const NativeComponentValue& component : components) {
            if (component.data != nullptr) {
                table.WriteComponent(location, component.type.id, component.data, component.type.size);
            }
        }
        return entity;
    }

    [[nodiscard]] std::vector<Entity> CreateEntities(std::size_t count, std::span<const NativeBulkComponentColumn> components) {
        if (count == 0U) {
            return {};
        }

        const std::vector<NativeComponentType> types = NormalizeTypes(components);
        ValidateAppendBulkColumnSourceCounts(components, count);
        const std::size_t tableIndex = FindOrCreateTable(types);
        std::vector<Entity> entities;
        entities.reserve(count);
        records_.reserve(records_.size() + count);
        for (std::size_t index = 0; index < count; ++index) {
            entities.push_back(AllocateEntity());
        }
        AppendEntitiesToTable(tableIndex, entities, components, true);
        return entities;
    }

    void AdoptEntity(Entity entity, std::span<const NativeComponentValue> components) {
        if (!entity.IsValid() || EntityIndex(entity) == kInvalidEntityIndex) {
            throw std::invalid_argument("Native ECS cannot adopt an invalid entity");
        }

        const std::vector<NativeComponentType> types = NormalizeTypes(components);
        if (entitySlots_.find(entity.Id()) != entitySlots_.end()) {
            throw std::invalid_argument("Native ECS cannot adopt an already live entity");
        }

        const std::uint32_t index = AllocateExternalRecord(entity);
        EntityRecord& record = records_[index];
        const std::uint32_t generation = EntityGeneration(entity);
        record.generation = generation;
        record.alive = true;
        record.ownsGeneratedId = false;
        record.entity = entity;
        entitySlots_[entity.Id()] = index;
        strippedEntitySlots_[StripEntityGeneration(entity)] = index;
        ++liveEntities_;

        const std::size_t tableIndex = FindOrCreateTable(types);
        ArchetypeTable& table = tables_[tableIndex];
        EntityLocation location = table.Add(entity);
        location.table = tableIndex;
        record.location = location;

        for (const NativeComponentValue& component : components) {
            if (component.data != nullptr) {
                table.WriteComponent(location, component.type.id, component.data, component.type.size);
            }
        }
    }

    void AdoptEntities(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
        if (entities.empty()) {
            return;
        }

        auto& uniqueIds = ResetUniqueIds(entities.size());
        for (Entity entity : entities) {
            if (!entity.IsValid() || EntityIndex(entity) == kInvalidEntityIndex || !uniqueIds.insert(entity.Id()).second ||
                entitySlots_.find(entity.Id()) != entitySlots_.end()) {
                throw std::invalid_argument("Native ECS cannot bulk adopt invalid, duplicate, or already live entities");
            }
        }

        const std::vector<NativeComponentType> types = NormalizeTypes(components);
        ValidateAppendBulkColumnSourceCounts(components, entities.size());
        const std::size_t tableIndex = FindOrCreateTable(types);

        records_.reserve(records_.size() + entities.size());
        auto& adopted = ResetAdoptedEntities(entities.size());
        try {
            for (Entity entity : entities) {
                const std::uint32_t index = AllocateExternalRecord(entity);
                EntityRecord& record = records_[index];
                record.generation = EntityGeneration(entity);
                record.alive = true;
                record.ownsGeneratedId = false;
                record.entity = entity;
                entitySlots_[entity.Id()] = index;
                strippedEntitySlots_[StripEntityGeneration(entity)] = index;
                ++liveEntities_;
                adopted.push_back(entity);
            }
            AppendEntitiesToTable(tableIndex, adopted, components);
        } catch (...) {
            for (Entity entity : adopted) {
                const auto slot = entitySlots_.find(entity.Id());
                if (slot != entitySlots_.end() && slot->second < records_.size()) {
                    records_[slot->second].alive = false;
                    strippedEntitySlots_.erase(StripEntityGeneration(entity));
                    entitySlots_.erase(slot);
                    --liveEntities_;
                }
            }
            throw;
        }
    }

    void DestroyEntity(Entity entity) {
        const std::uint32_t recordIndex = RecordIndex(entity);
        EntityRecord& record = LiveRecord(entity);
        ArchetypeTable& table = tables_[record.location.table];
        const Entity movedEntity = table.RemoveAt(record.location);
        if (movedEntity.IsValid()) {
            records_[RecordIndex(movedEntity)].location = record.location;
        }
        record.alive = false;
        entitySlots_.erase(entity.Id());
        if (record.ownsGeneratedId) {
            ++record.generation;
            freeEntityIndices_.push_back(recordIndex);
        }
        --liveEntities_;
    }

    void DestroyEntities(std::span<const Entity> entities) {
        if (entities.empty()) {
            return;
        }

        auto& uniqueIds = ResetUniqueIds(entities.size());
        auto& recordIndices = ResetRecordIndices(entities.size());
        auto& locationsByTable = ResetDestroyLocationGroups();
        for (Entity entity : entities) {
            if (!entity.IsValid() || !uniqueIds.insert(entity.Id()).second) {
                throw std::invalid_argument("Native ECS bulk destroy received an invalid or duplicate entity");
            }
            const std::uint32_t recordIndex = RecordIndex(entity);
            const EntityRecord& record = records_[recordIndex];
            recordIndices.push_back(recordIndex);
            locationsByTable[record.location.table].push_back(record.location);
        }

        for (auto& [tableIndex, locations] : locationsByTable) {
            if (locations.empty()) {
                continue;
            }
            tables_[tableIndex].RemoveMany(locations, movedEntitiesScratch_, removedRowsScratch_);
            for (auto& [movedEntity, location] : movedEntitiesScratch_) {
                location.table = tableIndex;
                records_[RecordIndex(movedEntity)].location = location;
            }
        }

        for (std::uint32_t recordIndex : recordIndices) {
            EntityRecord& record = records_[recordIndex];
            record.alive = false;
            entitySlots_.erase(record.entity.Id());
            strippedEntitySlots_.erase(StripEntityGeneration(record.entity));
            if (record.ownsGeneratedId) {
                ++record.generation;
                freeEntityIndices_.push_back(recordIndex);
            }
        }
        liveEntities_ -= entities.size();
    }

    [[nodiscard]] bool IsAlive(Entity entity) const noexcept {
        const std::uint32_t generatedIndex = EntityIndex(entity);
        if (generatedIndex != kInvalidEntityIndex && generatedIndex < records_.size()) {
            const EntityRecord& record = records_[generatedIndex];
            if (record.ownsGeneratedId) {
                return record.alive && record.entity == entity && record.generation == EntityGeneration(entity);
            }
        }

        const auto found = entitySlots_.find(entity.Id());
        if (found == entitySlots_.end() || found->second >= records_.size()) {
            return false;
        }
        const EntityRecord& record = records_[found->second];
        return record.alive && record.entity == entity && record.generation == EntityGeneration(entity);
    }

    [[nodiscard]] Entity ResolveAliveEntity(Entity::IdType entityIdWithoutGeneration) const noexcept {
        if (entityIdWithoutGeneration == 0) {
            return {};
        }

        const Entity::IdType strippedId = entityIdWithoutGeneration & 0xFFFFFFFFULL;
        const std::uint32_t generatedIndex = EntityIndex(Entity{ strippedId });
        if (generatedIndex != kInvalidEntityIndex && generatedIndex < records_.size()) {
            const EntityRecord& record = records_[generatedIndex];
            if (record.alive && StripEntityGeneration(record.entity) == strippedId) {
                return record.entity;
            }
        }

        const Entity candidate{ entityIdWithoutGeneration };
        if (IsAlive(candidate)) {
            return candidate;
        }

        const auto found = strippedEntitySlots_.find(strippedId);
        if (found == strippedEntitySlots_.end() || found->second >= records_.size()) {
            return {};
        }
        const EntityRecord& record = records_[found->second];
        return record.alive && StripEntityGeneration(record.entity) == strippedId ? record.entity : Entity{};
    }

    void AddComponents(Entity entity, std::span<const NativeComponentValue> components) {
        if (components.empty()) {
            return;
        }
        const std::vector<NativeComponentType> addedTypes = NormalizeTypes(components);
        EntityRecord& record = LiveRecord(entity);
        const std::size_t sourceIndex = record.location.table;
        const ArchetypeTable& source = tables_[sourceIndex];
        auto& targetTypes = ResetTargetTypesFrom(source.Types());
        for (const NativeComponentType& addedType : addedTypes) {
            const auto existing = std::lower_bound(targetTypes.begin(), targetTypes.end(), addedType.id, [](const NativeComponentType& type, ComponentId id) {
                return type.id < id;
            });
            if (existing != targetTypes.end() && existing->id == addedType.id) {
                throw std::invalid_argument("Native ECS component already exists on entity");
            }
            targetTypes.insert(existing, addedType);
        }
        Migrate(entity, record, sourceIndex, EdgeKind::Add, ComponentIds(addedTypes), targetTypes, components);
    }

    void AddComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
        if (entities.empty() || components.empty()) {
            return;
        }

        const std::vector<NativeComponentType> addedTypes = NormalizeTypes(components);
        ValidateRowMappedBulkColumnSourceCounts(components, entities.size());
        auto& uniqueIds = ResetUniqueIds(entities.size());
        auto& groups = ResetMigrationGroups();
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const Entity entity = entities[entityIndex];
            if (!entity.IsValid() || !uniqueIds.insert(entity.Id()).second) {
                throw std::invalid_argument("Native ECS bulk add received an invalid or duplicate entity");
            }

            const std::uint32_t recordIndex = RecordIndex(entity);
            const EntityRecord& record = records_[recordIndex];
            const std::size_t sourceIndex = record.location.table;
            const ArchetypeTable& source = tables_[sourceIndex];
            for (const NativeComponentType& addedType : addedTypes) {
                if (source.HasComponent(addedType.id)) {
                    throw std::invalid_argument("Native ECS bulk add component already exists on an entity");
                }
            }

            BulkMigrationGroup& group = groups[sourceIndex];
            group.entities.push_back(entity);
            group.sourceLocations.push_back(record.location);
            group.sourceRows.push_back(entityIndex);
            group.recordIndices.push_back(recordIndex);
            group.inputRowCount = entities.size();
        }

        for (auto& [sourceIndex, group] : groups) {
            if (group.entities.empty()) {
                continue;
            }
            const ArchetypeTable& source = tables_[sourceIndex];
            auto& targetTypes = ResetTargetTypesFrom(source.Types());
            for (const NativeComponentType& addedType : addedTypes) {
                const auto existing = std::lower_bound(targetTypes.begin(), targetTypes.end(), addedType.id, [](const NativeComponentType& type, ComponentId id) {
                    return type.id < id;
                });
                targetTypes.insert(existing, addedType);
            }

            BulkMigrate(
                sourceIndex,
                EdgeKind::Add,
                ComponentIds(addedTypes),
                targetTypes,
                group,
                components);
        }
    }

    void RemoveComponents(Entity entity, std::span<const ComponentId> componentIds) {
        if (componentIds.empty()) {
            return;
        }
        const std::vector<ComponentId> removedIds = NormalizeComponentIds(componentIds);
        EntityRecord& record = LiveRecord(entity);
        const std::size_t sourceIndex = record.location.table;
        const ArchetypeTable& source = tables_[sourceIndex];
        auto& targetTypes = ResetTargetTypes(source.Types().size());
        targetTypes.reserve(source.Types().size());
        for (const NativeComponentType& type : source.Types()) {
            if (!std::binary_search(removedIds.begin(), removedIds.end(), type.id)) {
                targetTypes.push_back(type);
            }
        }
        if (targetTypes.size() + removedIds.size() != source.Types().size()) {
            throw std::out_of_range("Native ECS component removal references a missing component");
        }
        Migrate(entity, record, sourceIndex, EdgeKind::Remove, removedIds, targetTypes, {});
    }

    void RemoveComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds) {
        if (entities.empty() || componentIds.empty()) {
            return;
        }

        const std::vector<ComponentId> removedIds = NormalizeComponentIds(componentIds);
        auto& uniqueIds = ResetUniqueIds(entities.size());
        auto& groups = ResetMigrationGroups();
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const Entity entity = entities[entityIndex];
            if (!entity.IsValid() || !uniqueIds.insert(entity.Id()).second) {
                throw std::invalid_argument("Native ECS bulk remove received an invalid or duplicate entity");
            }

            const std::uint32_t recordIndex = RecordIndex(entity);
            const EntityRecord& record = records_[recordIndex];
            const ArchetypeTable& source = tables_[record.location.table];
            for (ComponentId removedId : removedIds) {
                if (!source.HasComponent(removedId)) {
                    throw std::out_of_range("Native ECS bulk remove references a missing component");
                }
            }

            BulkMigrationGroup& group = groups[record.location.table];
            group.entities.push_back(entity);
            group.sourceLocations.push_back(record.location);
            group.sourceRows.push_back(entityIndex);
            group.recordIndices.push_back(recordIndex);
        }

        for (auto& [sourceIndex, group] : groups) {
            if (group.entities.empty()) {
                continue;
            }
            const ArchetypeTable& source = tables_[sourceIndex];
            auto& targetTypes = ResetTargetTypes(source.Types().size());
            targetTypes.reserve(source.Types().size());
            for (const NativeComponentType& type : source.Types()) {
                if (!std::binary_search(removedIds.begin(), removedIds.end(), type.id)) {
                    targetTypes.push_back(type);
                }
            }

            BulkMigrate(sourceIndex, EdgeKind::Remove, removedIds, targetTypes, group, {});
        }
    }

    void SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size) {
        EntityRecord& record = LiveRecord(entity);
        tables_[record.location.table].WriteComponent(record.location, componentId, data, size);
    }

    void SetComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
        if (entities.empty() || components.empty()) {
            return;
        }

        const std::vector<NativeComponentType> types = NormalizeTypes(components);
        ValidateRowMappedBulkColumnSourceCounts(components, entities.size());
        auto& uniqueIds = ResetUniqueIds(entities.size());
        auto& groups = ResetMigrationGroups();
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const Entity entity = entities[entityIndex];
            if (!entity.IsValid() || !uniqueIds.insert(entity.Id()).second) {
                throw std::invalid_argument("Native ECS bulk set received an invalid or duplicate entity");
            }

            const std::uint32_t recordIndex = RecordIndex(entity);
            const EntityRecord& record = records_[recordIndex];
            const ArchetypeTable& table = tables_[record.location.table];
            for (const NativeComponentType& type : types) {
                if (!table.HasComponent(type.id)) {
                    throw std::out_of_range("Native ECS bulk set references a missing component");
                }
            }

            BulkMigrationGroup& group = groups[record.location.table];
            group.sourceLocations.push_back(record.location);
            group.sourceRows.push_back(entityIndex);
            group.inputRowCount = entities.size();
        }

        for (auto& [tableIndex, group] : groups) {
            if (group.sourceLocations.empty()) {
                continue;
            }
            ArchetypeTable& table = tables_[tableIndex];
            for (const NativeBulkComponentColumn& component : components) {
                const std::size_t stride = ResolveRowMappedBulkColumnStride(component, group.inputRowCount);
                table.WriteComponentRows(group.sourceLocations, component.type.id, component.data, group.sourceRows, stride);
            }
        }
    }

    void MarkComponentModified(Entity entity, ComponentId componentId) {
        EntityRecord& record = LiveRecord(entity);
        tables_[record.location.table].MarkComponentsModified(std::span<const ComponentId>{ &componentId, 1U });
    }

    void MarkArchetypeComponentsModified(std::size_t archetypeIndex, std::span<const ComponentId> componentIds) {
        if (archetypeIndex >= tables_.size()) {
            throw std::out_of_range("Native ECS archetype index is invalid");
        }
        tables_[archetypeIndex].MarkComponentsModified(componentIds);
    }

    [[nodiscard]] void* MutableComponentData(Entity entity, ComponentId componentId) {
        EntityRecord& record = LiveRecord(entity);
        return tables_[record.location.table].ComponentData(record.location, componentId);
    }

    [[nodiscard]] const void* ComponentData(Entity entity, ComponentId componentId) const {
        const EntityRecord& record = LiveRecord(entity);
        return tables_[record.location.table].ComponentData(record.location, componentId);
    }

    [[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const {
        const EntityRecord& record = LiveRecord(entity);
        const std::optional<std::size_t> bitIndex = signatureRegistry_.FindBit(componentId);
        return bitIndex.has_value() && tables_[record.location.table].HasSignatureBit(*bitIndex);
    }

    [[nodiscard]] bool EntityArchetypeMatches(Entity entity, std::span<const ComponentId> requiredComponentIds) const {
        const std::vector<ComponentId> requiredIds = NormalizeComponentIds(requiredComponentIds);
        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds);
        if (!requiredSignature.has_value()) {
            return false;
        }

        const EntityRecord& record = LiveRecord(entity);
        return tables_[record.location.table].Matches(*requiredSignature);
    }

    [[nodiscard]] std::vector<NativeArchetypeMatch> MatchingArchetypes(std::span<const ComponentId> requiredComponentIds) const {
        const std::vector<ComponentId> requiredIds = NormalizeComponentIds(requiredComponentIds);
        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds);
        if (!requiredSignature.has_value()) {
            return {};
        }

        std::vector<NativeArchetypeMatch> matches;
        matches.reserve(tables_.size());
        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (table.Matches(*requiredSignature)) {
                matches.push_back(NativeArchetypeMatch{
                    .archetypeIndex = tableIndex,
                    .liveEntities = table.LiveEntities(),
                    .version = table.Version(),
                });
            }
        }
        return matches;
    }

    void CollectQueryRecords(
        std::span<const ComponentId> componentIds,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::vector<QueryTableDispatchRecord>& records) const {
        records.clear();
        if (componentIds.empty() || componentIds.size() > kQueryExecutionScratchMaxTerms) {
            return;
        }

        StackComponentIdSet requiredIds;
        if (!requiredIds.AssignUnion(componentIds, requiredComponentIds)) {
            return;
        }

        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds.Values());
        if (!requiredSignature.has_value()) {
            return;
        }
        StackComponentIdSet excludedIds;
        if (!excludedIds.Assign(excludedComponentIds)) {
            return;
        }
        const std::span<const ComponentId> excludedIdValues = excludedIds.Values();

        std::size_t sequence = 0;
        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (!table.Matches(*requiredSignature) || HasExcludedComponent(table, excludedIdValues)) {
                continue;
            }
            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                if (rowCount == 0) {
                    continue;
                }

                QueryTableDispatchRecord record{
                    .table = nullptr,
                    .entityIds = table.ChunkEntityIds(chunkIndex),
                    .entityCount = rowCount,
                    .nativeArchetypeIndex = tableIndex,
                    .nativeChunkIndex = chunkIndex,
                    .firstEntityId = table.ChunkEntityIds(chunkIndex)[0],
                    .sequence = sequence++,
                };
                for (std::size_t field = 0; field < componentIds.size(); ++field) {
                    record.fieldComponents[field] = table.ComponentColumnData(chunkIndex, componentIds[field]);
                    record.componentVersions[field] = table.ComponentVersionOrZero(componentIds[field]);
                }
                records.push_back(record);
            }
        }
    }

    void CollectMutableQueryRecords(
        std::span<const ComponentId> componentIds,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::vector<MutableQueryTableDispatchRecord>& records) {
        records.clear();
        if (componentIds.empty() || componentIds.size() > kQueryExecutionScratchMaxTerms) {
            return;
        }

        StackComponentIdSet requiredIds;
        if (!requiredIds.AssignUnion(componentIds, requiredComponentIds)) {
            return;
        }

        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds.Values());
        if (!requiredSignature.has_value()) {
            return;
        }
        StackComponentIdSet excludedIds;
        if (!excludedIds.Assign(excludedComponentIds)) {
            return;
        }
        const std::span<const ComponentId> excludedIdValues = excludedIds.Values();

        std::size_t sequence = 0;
        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            ArchetypeTable& table = tables_[tableIndex];
            if (!table.Matches(*requiredSignature) || HasExcludedComponent(table, excludedIdValues)) {
                continue;
            }
            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                if (rowCount == 0) {
                    continue;
                }

                MutableQueryTableDispatchRecord record{
                    .table = nullptr,
                    .entityIds = table.ChunkEntityIds(chunkIndex),
                    .entityCount = rowCount,
                    .nativeArchetypeIndex = tableIndex,
                    .nativeChunkIndex = chunkIndex,
                    .firstEntityId = table.ChunkEntityIds(chunkIndex)[0],
                    .sequence = sequence++,
                };
                for (std::size_t field = 0; field < componentIds.size(); ++field) {
                    record.fieldComponents[field] = table.MutableComponentColumnData(chunkIndex, componentIds[field]);
                    record.componentVersions[field] = table.ComponentVersionOrZero(componentIds[field]);
                }
                records.push_back(record);
            }
        }
    }

    [[nodiscard]] std::uint64_t ArchetypeVersion(Entity entity) const {
        const EntityRecord& record = LiveRecord(entity);
        return tables_[record.location.table].Version();
    }

    [[nodiscard]] std::uint64_t ComponentVersion(Entity entity, ComponentId componentId) const {
        const EntityRecord& record = LiveRecord(entity);
        return tables_[record.location.table].ComponentVersion(componentId);
    }

    [[nodiscard]] std::uint64_t ArchetypeComponentVersion(std::size_t archetypeIndex, ComponentId componentId) const {
        if (archetypeIndex >= tables_.size()) {
            throw std::out_of_range("Native ECS archetype index is invalid");
        }
        return tables_[archetypeIndex].ComponentVersion(componentId);
    }

    [[nodiscard]] std::size_t ChunkPayloadBytes() const noexcept {
        return chunkPayloadBytes_;
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept {
        return pool_.ChunksInUse();
    }

    void CaptureChunkedSnapshot(std::span<const ComponentTypeInfo> componentTypes, ChunkedWorldSnapshot& snapshot) const {
        snapshot = {};
        snapshot.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        snapshot.entityCount = liveEntities_;
        snapshot.chunks.reserve(pool_.ChunksInUse());

        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (table.LiveEntities() == 0U) {
                continue;
            }

            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                if (rowCount == 0U) {
                    continue;
                }

                ChunkedWorldSnapshotChunk& chunk = snapshot.chunks.emplace_back();
                chunk.archetypeIndex = tableIndex;
                chunk.chunkIndex = chunkIndex;

                const Entity::IdType* entityIds = table.ChunkEntityIds(chunkIndex);
                chunk.entityIds.assign(entityIds, entityIds + rowCount);
                chunk.components.reserve(table.Types().size());

                for (const NativeComponentType& type : table.Types()) {
                    const ComponentTypeInfo* componentType = FindComponentType(componentTypes, type.id);
                    ChunkedComponentSnapshot& component = chunk.components.emplace_back();
                    component.componentId = type.id;
                    component.componentName = componentType != nullptr ? componentType->name : std::string{};
                    component.componentSize = type.size;
                    component.version = table.ComponentVersionOrZero(type.id);

                    const std::size_t byteCount = rowCount * type.size;
                    component.data.resize(byteCount);
                    std::memcpy(component.data.data(), table.ComponentColumnData(chunkIndex, type.id), byteCount);
                }
            }
        }
    }

    void CaptureChunkedDeltaSnapshot(
        std::span<const ComponentTypeInfo> componentTypes,
        const ChunkedWorldSnapshot& baseline,
        ChunkedWorldDeltaSnapshot& delta) const {
        delta = {};
        delta.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        delta.entityCount = liveEntities_;

        std::unordered_map<ChunkKey, const ChunkedWorldSnapshotChunk*, ChunkKeyHash> baselineChunks;
        baselineChunks.reserve(baseline.chunks.size());
        std::unordered_set<Entity::IdType> baselineEntities;
        baselineEntities.reserve(baseline.entityCount);
        for (const ChunkedWorldSnapshotChunk& chunk : baseline.chunks) {
            baselineChunks.emplace(
                ChunkKey{ .archetypeIndex = chunk.archetypeIndex, .chunkIndex = chunk.chunkIndex },
                &chunk);
            for (Entity::IdType entityId : chunk.entityIds) {
                baselineEntities.insert(entityId);
            }
        }

        std::unordered_set<Entity::IdType> currentEntities;
        currentEntities.reserve(liveEntities_);
        delta.chunks.reserve(pool_.ChunksInUse());

        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (table.LiveEntities() == 0U) {
                continue;
            }

            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                if (rowCount == 0U) {
                    continue;
                }

                const Entity::IdType* entityIds = table.ChunkEntityIds(chunkIndex);
                for (std::size_t row = 0; row < rowCount; ++row) {
                    currentEntities.insert(entityIds[row]);
                }

                const auto baselineChunk = baselineChunks.find(ChunkKey{ .archetypeIndex = tableIndex, .chunkIndex = chunkIndex });
                const bool fullArchetype = baselineChunk == baselineChunks.end() ||
                    !SameEntityIds(*baselineChunk->second, entityIds, rowCount) ||
                    !SameComponentSet(*baselineChunk->second, table.Types());

                ChunkedWorldDeltaSnapshotChunk chunk;
                chunk.archetypeIndex = tableIndex;
                chunk.chunkIndex = chunkIndex;
                chunk.fullArchetype = fullArchetype;
                chunk.entityIds.assign(entityIds, entityIds + rowCount);
                chunk.components.reserve(table.Types().size());

                for (const NativeComponentType& type : table.Types()) {
                    const std::uint64_t currentVersion = table.ComponentVersionOrZero(type.id);
                    const ChunkedComponentSnapshot* baselineComponent = baselineChunk != baselineChunks.end()
                        ? FindSnapshotComponent(*baselineChunk->second, type.id)
                        : nullptr;
                    if (!fullArchetype && baselineComponent != nullptr && baselineComponent->version == currentVersion) {
                        continue;
                    }

                    const ComponentTypeInfo* componentType = FindComponentType(componentTypes, type.id);
                    ChunkedComponentSnapshot& component = chunk.components.emplace_back();
                    component.componentId = type.id;
                    component.componentName = componentType != nullptr ? componentType->name : std::string{};
                    component.componentSize = type.size;
                    component.version = currentVersion;

                    const std::size_t byteCount = rowCount * type.size;
                    component.data.resize(byteCount);
                    std::memcpy(component.data.data(), table.ComponentColumnData(chunkIndex, type.id), byteCount);
                }

                if (chunk.fullArchetype || !chunk.components.empty()) {
                    delta.chunks.push_back(std::move(chunk));
                }
            }
        }

        for (Entity::IdType entityId : baselineEntities) {
            if (currentEntities.find(entityId) == currentEntities.end()) {
                delta.destroyedEntityIds.push_back(entityId);
            }
        }
        std::sort(delta.destroyedEntityIds.begin(), delta.destroyedEntityIds.end());
    }

    [[nodiscard]] bool StreamChunkedSnapshot(
        std::span<const ComponentTypeInfo> componentTypes,
        ChunkedWorldSnapshotChunkVisitor visitor,
        void* context) const {
        if (visitor == nullptr) {
            return false;
        }

        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (table.LiveEntities() == 0U) {
                continue;
            }

            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                if (rowCount == 0U) {
                    continue;
                }

                std::vector<ChunkedComponentSnapshotView> components;
                components.reserve(table.Types().size());
                for (const NativeComponentType& type : table.Types()) {
                    const ComponentTypeInfo* componentType = FindComponentType(componentTypes, type.id);
                    components.push_back(ChunkedComponentSnapshotView{
                        .componentId = type.id,
                        .componentName = componentType != nullptr ? std::string_view{ componentType->name } : std::string_view{},
                        .componentSize = type.size,
                        .version = table.ComponentVersionOrZero(type.id),
                        .data = std::span<const std::byte>{
                            static_cast<const std::byte*>(table.ComponentColumnData(chunkIndex, type.id)),
                            rowCount * type.size,
                        },
                    });
                }

                const ChunkedWorldSnapshotChunkView chunk{
                    .archetypeIndex = tableIndex,
                    .chunkIndex = chunkIndex,
                    .entityIds = std::span<const Entity::IdType>{ table.ChunkEntityIds(chunkIndex), rowCount },
                    .components = std::span<const ChunkedComponentSnapshotView>{ components },
                };
                if (!visitor(chunk, context)) {
                    return false;
                }
            }
        }

        return true;
    }

    [[nodiscard]] NativeEcsStorageStats Stats() const {
        NativeEcsStorageStats stats;
        stats.chunks = pool_.ChunksInUse();
        stats.chunkPoolAllocated = pool_.AllocatedChunks();
        stats.chunkPoolInUse = pool_.ChunksInUse();
        stats.chunkPoolFree = pool_.FreeChunks();
        stats.chunkPoolAcquireCount = pool_.AcquireCount();
        stats.chunkPoolReuseCount = pool_.ReuseCount();
        stats.chunkPoolReleaseCount = pool_.ReleaseCount();
        stats.chunkPoolTrimCount = pool_.TrimCount();
        stats.archetypeCount = tables_.size();
        stats.liveEntities = liveEntities_;
        stats.archetypeCounters.reserve(tables_.size());

        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];

            NativeEcsArchetypeMemoryCounters& archetype = stats.archetypeCounters.emplace_back();
            archetype.archetypeIndex = tableIndex;
            archetype.liveEntities = table.LiveEntities();
            archetype.chunks = table.ChunkCount();
            archetype.capacity = table.Capacity() * table.ChunkCount();
            archetype.payloadBytes = table.ChunkCount() * chunkPayloadBytes_;
            archetype.usedBytes = table.UsedBytes();
            archetype.wastedBytes = archetype.payloadBytes - std::min(archetype.usedBytes, archetype.payloadBytes);
            archetype.version = table.Version();
            archetype.componentIds.reserve(table.Types().size());
            for (const NativeComponentType& type : table.Types()) {
                archetype.componentIds.push_back(type.id);
            }

            archetype.chunkCounters.reserve(table.ChunkCount());
            for (std::size_t chunkIndex = 0; chunkIndex < table.ChunkCount(); ++chunkIndex) {
                const std::size_t rowCount = table.ChunkRowCount(chunkIndex);
                const std::size_t usedBytes = table.ChunkUsedBytes(chunkIndex);
                if (rowCount == 0U) {
                    ++stats.emptyChunks;
                } else if (rowCount < table.Capacity()) {
                    ++stats.sparseChunks;
                    if (chunkIndex + 1U == table.ChunkCount()) {
                        ++stats.tailSparseChunks;
                    } else {
                        ++stats.fragmentedChunks;
                    }
                }
                archetype.chunkCounters.push_back(NativeEcsChunkMemoryCounters{
                    .archetypeIndex = tableIndex,
                    .chunkIndex = chunkIndex,
                    .liveEntities = rowCount,
                    .capacity = table.Capacity(),
                    .payloadBytes = chunkPayloadBytes_,
                    .usedBytes = usedBytes,
                    .wastedBytes = chunkPayloadBytes_ - std::min(usedBytes, chunkPayloadBytes_),
                });
            }

            stats.capacity += archetype.capacity;
            stats.usedBytes += archetype.usedBytes;
        }
        stats.wastedBytes = (stats.chunks * chunkPayloadBytes_) - std::min(stats.usedBytes, stats.chunks * chunkPayloadBytes_);
        return stats;
    }

    [[nodiscard]] NativeEcsMaintenanceStats MaintainChunks(NativeEcsMaintenanceBudget budget) {
        const NativeEcsStorageStats before = Stats();
        const std::size_t released = pool_.TrimFreeChunks(budget.maxFreeChunksToKeep, budget.maxChunksToRelease);
        const NativeEcsStorageStats after = Stats();
        return NativeEcsMaintenanceStats{
            .freeChunksBefore = before.chunkPoolFree,
            .freeChunksAfter = after.chunkPoolFree,
            .chunkPoolAllocatedBefore = before.chunkPoolAllocated,
            .chunkPoolAllocatedAfter = after.chunkPoolAllocated,
            .chunksReleasedToSystem = released,
            .fragmentedChunksBefore = before.fragmentedChunks,
            .fragmentedChunksAfter = after.fragmentedChunks,
            .emptyChunksBefore = before.emptyChunks,
            .emptyChunksAfter = after.emptyChunks,
            .budgetExhausted = after.chunkPoolFree > budget.maxFreeChunksToKeep,
        };
    }

private:
    [[nodiscard]] std::unordered_set<Entity::IdType>& ResetUniqueIds(std::size_t expectedCount) {
        uniqueIdsScratch_.clear();
        uniqueIdsScratch_.reserve(expectedCount);
        return uniqueIdsScratch_;
    }

    [[nodiscard]] std::vector<std::uint32_t>& ResetRecordIndices(std::size_t expectedCount) {
        recordIndicesScratch_.clear();
        recordIndicesScratch_.reserve(expectedCount);
        return recordIndicesScratch_;
    }

    [[nodiscard]] std::vector<Entity>& ResetAdoptedEntities(std::size_t expectedCount) {
        adoptedEntitiesScratch_.clear();
        adoptedEntitiesScratch_.reserve(expectedCount);
        return adoptedEntitiesScratch_;
    }

    [[nodiscard]] std::unordered_map<std::size_t, std::vector<EntityLocation>>& ResetDestroyLocationGroups() noexcept {
        for (auto& [tableIndex, locations] : destroyLocationsByTableScratch_) {
            static_cast<void>(tableIndex);
            locations.clear();
        }
        return destroyLocationsByTableScratch_;
    }

    [[nodiscard]] std::unordered_map<std::size_t, BulkMigrationGroup>& ResetMigrationGroups() noexcept {
        for (auto& [tableIndex, group] : migrationGroupsScratch_) {
            static_cast<void>(tableIndex);
            group.Clear();
        }
        return migrationGroupsScratch_;
    }

    [[nodiscard]] std::vector<NativeComponentType>& ResetTargetTypes(std::size_t expectedCount) {
        targetTypesScratch_.clear();
        targetTypesScratch_.reserve(expectedCount);
        return targetTypesScratch_;
    }

    [[nodiscard]] std::vector<NativeComponentType>& ResetTargetTypesFrom(std::span<const NativeComponentType> sourceTypes) {
        auto& targetTypes = ResetTargetTypes(sourceTypes.size());
        targetTypes.insert(targetTypes.end(), sourceTypes.begin(), sourceTypes.end());
        return targetTypes;
    }

    void BulkMigrate(
        std::size_t sourceIndex,
        EdgeKind edgeKind,
        std::vector<ComponentId> edgeComponents,
        std::span<const NativeComponentType> targetTypes,
        BulkMigrationGroup& group,
        std::span<const NativeBulkComponentColumn> addedComponents) {
        const std::size_t targetIndex = ResolveMigrationTarget(sourceIndex, edgeKind, std::move(edgeComponents), targetTypes);
        ArchetypeTable& target = tables_[targetIndex];
        auto& targetLocations = targetLocationsScratch_;
        target.AddMany(group.entities, targetIndex, targetLocations, false);
        if (targetLocations.size() != group.entities.size()) {
            throw std::runtime_error("Native ECS bulk migration returned an invalid target row count");
        }

        tables_[sourceIndex].CopyEntitiesTo(group.sourceLocations, target, targetLocations);
        for (const NativeBulkComponentColumn& component : addedComponents) {
            const std::size_t stride = ResolveRowMappedBulkColumnStride(component, group.inputRowCount);
            target.WriteComponentRows(targetLocations, component.type.id, component.data, group.sourceRows, stride);
        }

        tables_[sourceIndex].RemoveMany(group.sourceLocations, movedEntitiesScratch_, removedRowsScratch_);
        for (auto& [movedEntity, location] : movedEntitiesScratch_) {
            location.table = sourceIndex;
            records_[RecordIndex(movedEntity)].location = location;
        }

        for (std::size_t index = 0; index < group.recordIndices.size(); ++index) {
            records_[group.recordIndices[index]].location = targetLocations[index];
        }
    }

    void AppendEntitiesToTable(
        std::size_t tableIndex,
        std::span<const Entity> entities,
        std::span<const NativeBulkComponentColumn> components,
        bool generatedOwnedEntities = false) {
        ArchetypeTable& table = tables_[tableIndex];
        auto& locations = appendLocationsScratch_;
        table.AddMany(entities, tableIndex, locations, false);
        if (locations.size() != entities.size()) {
            throw std::runtime_error("Native ECS bulk append returned an invalid location count");
        }

        if (generatedOwnedEntities) {
            for (std::size_t index = 0; index < entities.size(); ++index) {
                const std::uint32_t recordIndex = EntityIndex(entities[index]);
                assert(recordIndex != kInvalidEntityIndex);
                assert(recordIndex < records_.size());
                assert(records_[recordIndex].entity == entities[index]);
                assert(records_[recordIndex].alive);
                assert(records_[recordIndex].ownsGeneratedId);
                records_[recordIndex].location = locations[index];
            }
        } else {
            for (std::size_t index = 0; index < entities.size(); ++index) {
                records_[RecordIndex(entities[index])].location = locations[index];
            }
        }

        for (const NativeBulkComponentColumn& component : components) {
            const bool broadcast = component.sourceCount == 1U;
            const bool repeatedPattern = component.sourceCount > 1U && component.sourceCount < entities.size();
            const std::size_t stride = broadcast ? 0U : (component.stride == 0U ? component.type.size : component.stride);
            std::size_t consumed = 0;
            while (consumed < locations.size()) {
                const EntityLocation first = locations[consumed];
                std::size_t count = 1U;
                while (consumed + count < locations.size()) {
                    const EntityLocation next = locations[consumed + count];
                    if (next.chunk != first.chunk || next.row != first.row + count) {
                        break;
                    }
                    ++count;
                }

                const auto* data = broadcast ? component.data : static_cast<const std::byte*>(component.data) + (consumed * stride);
                if (repeatedPattern) {
                    table.WriteComponentColumnPattern(first.chunk, first.row, component.type.id, component.data, count, stride, component.sourceCount, consumed);
                } else {
                    table.WriteComponentColumn(first.chunk, first.row, component.type.id, data, count, stride);
                }
                consumed += count;
            }
        }
    }

    [[nodiscard]] static std::vector<ComponentId> ComponentIds(std::span<const NativeComponentType> types) {
        std::vector<ComponentId> ids;
        ids.reserve(types.size());
        for (const NativeComponentType& type : types) {
            ids.push_back(type.id);
        }
        return ids;
    }

    [[nodiscard]] static bool HasExcludedComponent(const ArchetypeTable& table, std::span<const ComponentId> excludedIds) noexcept {
        for (ComponentId componentId : excludedIds) {
            if (table.HasComponent(componentId)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] Entity AllocateEntity() {
        ++liveEntities_;
        if (!freeEntityIndices_.empty()) {
            const std::uint32_t index = freeEntityIndices_.back();
            freeEntityIndices_.pop_back();
            EntityRecord& record = records_[index];
            record.alive = true;
            record.ownsGeneratedId = true;
            record.entity = PackEntity(index, record.generation);
            return record.entity;
        }

        if (records_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("Native ECS entity capacity exceeded");
        }
        const std::uint32_t index = static_cast<std::uint32_t>(records_.size());
        Entity entity = PackEntity(index, 0U);
        records_.push_back(EntityRecord{
            .generation = 0U,
            .alive = true,
            .ownsGeneratedId = true,
            .entity = entity,
        });
        return entity;
    }

    [[nodiscard]] std::uint32_t AllocateExternalRecord(Entity entity) {
        if (records_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("Native ECS entity capacity exceeded");
        }
        const std::uint32_t index = static_cast<std::uint32_t>(records_.size());
        records_.push_back(EntityRecord{
            .generation = EntityGeneration(entity),
            .alive = false,
            .ownsGeneratedId = false,
            .entity = entity,
        });
        return index;
    }

    [[nodiscard]] std::uint32_t RecordIndexUnchecked(Entity entity) const {
        const std::uint32_t generatedIndex = EntityIndex(entity);
        if (generatedIndex != kInvalidEntityIndex && generatedIndex < records_.size() && records_[generatedIndex].ownsGeneratedId) {
            return generatedIndex;
        }

        const auto found = entitySlots_.find(entity.Id());
        if (found == entitySlots_.end()) {
            throw std::out_of_range("Invalid native ECS entity");
        }
        return found->second;
    }

    [[nodiscard]] std::uint32_t RecordIndex(Entity entity) const {
        const std::uint32_t index = RecordIndexUnchecked(entity);
        if (index >= records_.size() || !records_[index].alive || records_[index].entity != entity || records_[index].generation != EntityGeneration(entity)) {
            throw std::out_of_range("Invalid or stale native ECS entity");
        }
        return index;
    }

    [[nodiscard]] EntityRecord& LiveRecord(Entity entity) {
        return records_[RecordIndex(entity)];
    }

    [[nodiscard]] const EntityRecord& LiveRecord(Entity entity) const {
        return records_[RecordIndex(entity)];
    }

    [[nodiscard]] std::size_t FindOrCreateTable(std::span<const NativeComponentType> types) {
        const ComponentSignature signature = signatureRegistry_.Build(types);
        for (std::size_t index = 0; index < tables_.size(); ++index) {
            if (tables_[index].Signature() == signature && SameTypes(tables_[index].Types(), types)) {
                return index;
            }
        }
        tables_.emplace_back(pool_, std::vector<NativeComponentType>(types.begin(), types.end()), signature);
        return tables_.size() - 1U;
    }

    [[nodiscard]] std::size_t ResolveMigrationTarget(
        std::size_t sourceIndex,
        EdgeKind edgeKind,
        std::vector<ComponentId> edgeComponents,
        std::span<const NativeComponentType> targetTypes) {
        EdgeKey key{ .kind = edgeKind, .componentIds = std::move(edgeComponents) };
        auto found = tables_[sourceIndex].Edges().find(key);
        if (found != tables_[sourceIndex].Edges().end()) {
            return found->second;
        }
        const std::size_t targetIndex = FindOrCreateTable(targetTypes);
        tables_[sourceIndex].Edges().emplace(std::move(key), targetIndex);
        return targetIndex;
    }

    void Migrate(
        Entity entity,
        EntityRecord& record,
        std::size_t sourceIndex,
        EdgeKind edgeKind,
        std::vector<ComponentId> edgeComponents,
        std::span<const NativeComponentType> targetTypes,
        std::span<const NativeComponentValue> addedComponents) {
        const EntityLocation sourceLocation = record.location;
        const std::size_t targetIndex = ResolveMigrationTarget(sourceIndex, edgeKind, std::move(edgeComponents), targetTypes);
        ArchetypeTable& source = tables_[sourceIndex];
        ArchetypeTable& target = tables_[targetIndex];
        EntityLocation targetLocation = target.Add(entity);
        targetLocation.table = targetIndex;

        source.CopyEntityTo(sourceLocation, target, targetLocation);
        for (const NativeComponentValue& component : addedComponents) {
            if (component.data != nullptr) {
                target.WriteComponent(targetLocation, component.type.id, component.data, component.type.size);
            }
        }

        const Entity movedEntity = source.RemoveAt(sourceLocation);
        if (movedEntity.IsValid()) {
            records_[RecordIndex(movedEntity)].location = sourceLocation;
        }
        record.location = targetLocation;
    }

    std::size_t chunkPayloadBytes_ = 0;
    NativeChunkPool pool_;
    std::vector<EntityRecord> records_;
    std::vector<std::uint32_t> freeEntityIndices_;
    std::unordered_map<Entity::IdType, std::uint32_t> entitySlots_;
    std::unordered_map<Entity::IdType, std::uint32_t> strippedEntitySlots_;
    ComponentSignatureRegistry signatureRegistry_;
    std::vector<ArchetypeTable> tables_;
    std::size_t liveEntities_ = 0;
    std::unordered_set<Entity::IdType> uniqueIdsScratch_;
    std::vector<std::uint32_t> recordIndicesScratch_;
    std::unordered_map<std::size_t, std::vector<EntityLocation>> destroyLocationsByTableScratch_;
    std::unordered_map<std::size_t, BulkMigrationGroup> migrationGroupsScratch_;
    std::vector<Entity> adoptedEntitiesScratch_;
    std::vector<EntityLocation> appendLocationsScratch_;
    std::vector<EntityLocation> targetLocationsScratch_;
    std::vector<std::pair<Entity, EntityLocation>> movedEntitiesScratch_;
    std::vector<std::size_t> removedRowsScratch_;
    std::vector<NativeComponentType> targetTypesScratch_;
};

NativeArchetypeStorage::NativeArchetypeStorage(WorldConfig config)
    : impl_(new Impl(config)) {}

NativeArchetypeStorage::~NativeArchetypeStorage() {
    delete impl_;
}

NativeArchetypeStorage::NativeArchetypeStorage(NativeArchetypeStorage&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}

NativeArchetypeStorage& NativeArchetypeStorage::operator=(NativeArchetypeStorage&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

Entity NativeArchetypeStorage::CreateEntity(std::span<const NativeComponentValue> components) {
    return impl_->CreateEntity(components);
}

std::vector<Entity> NativeArchetypeStorage::CreateEntities(std::size_t count, std::span<const NativeBulkComponentColumn> components) {
    return impl_->CreateEntities(count, components);
}

void NativeArchetypeStorage::AdoptEntity(Entity entity, std::span<const NativeComponentValue> components) {
    impl_->AdoptEntity(entity, components);
}

void NativeArchetypeStorage::AdoptEntities(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
    impl_->AdoptEntities(entities, components);
}

void NativeArchetypeStorage::DestroyEntity(Entity entity) {
    impl_->DestroyEntity(entity);
}

void NativeArchetypeStorage::DestroyEntities(std::span<const Entity> entities) {
    impl_->DestroyEntities(entities);
}

bool NativeArchetypeStorage::IsAlive(Entity entity) const noexcept {
    return impl_ != nullptr && impl_->IsAlive(entity);
}

Entity NativeArchetypeStorage::ResolveAliveEntity(Entity::IdType entityIdWithoutGeneration) const noexcept {
    return impl_ != nullptr ? impl_->ResolveAliveEntity(entityIdWithoutGeneration) : Entity{};
}

void NativeArchetypeStorage::AddComponents(Entity entity, std::span<const NativeComponentValue> components) {
    impl_->AddComponents(entity, components);
}

void NativeArchetypeStorage::AddComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
    impl_->AddComponents(entities, components);
}

void NativeArchetypeStorage::RemoveComponents(Entity entity, std::span<const ComponentId> componentIds) {
    impl_->RemoveComponents(entity, componentIds);
}

void NativeArchetypeStorage::RemoveComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds) {
    impl_->RemoveComponents(entities, componentIds);
}

void NativeArchetypeStorage::SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size) {
    impl_->SetComponent(entity, componentId, data, size);
}

void NativeArchetypeStorage::SetComponents(std::span<const Entity> entities, std::span<const NativeBulkComponentColumn> components) {
    impl_->SetComponents(entities, components);
}

void NativeArchetypeStorage::MarkComponentModified(Entity entity, ComponentId componentId) {
    impl_->MarkComponentModified(entity, componentId);
}

void NativeArchetypeStorage::MarkArchetypeComponentsModified(std::size_t archetypeIndex, std::span<const ComponentId> componentIds) {
    impl_->MarkArchetypeComponentsModified(archetypeIndex, componentIds);
}

void* NativeArchetypeStorage::MutableComponentData(Entity entity, ComponentId componentId) {
    return impl_->MutableComponentData(entity, componentId);
}

const void* NativeArchetypeStorage::ComponentData(Entity entity, ComponentId componentId) const {
    return impl_->ComponentData(entity, componentId);
}

bool NativeArchetypeStorage::HasComponent(Entity entity, ComponentId componentId) const {
    return impl_->HasComponent(entity, componentId);
}

bool NativeArchetypeStorage::EntityArchetypeMatches(Entity entity, std::span<const ComponentId> requiredComponentIds) const {
    return impl_->EntityArchetypeMatches(entity, requiredComponentIds);
}

std::vector<NativeArchetypeMatch> NativeArchetypeStorage::MatchingArchetypes(std::span<const ComponentId> requiredComponentIds) const {
    return impl_->MatchingArchetypes(requiredComponentIds);
}

void NativeArchetypeStorage::CollectQueryRecords(
    std::span<const ComponentId> componentIds,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::vector<QueryTableDispatchRecord>& records) const {
    impl_->CollectQueryRecords(componentIds, requiredComponentIds, excludedComponentIds, records);
}

void NativeArchetypeStorage::CollectMutableQueryRecords(
    std::span<const ComponentId> componentIds,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::vector<MutableQueryTableDispatchRecord>& records) {
    impl_->CollectMutableQueryRecords(componentIds, requiredComponentIds, excludedComponentIds, records);
}

void NativeArchetypeStorage::CaptureChunkedSnapshot(std::span<const ComponentTypeInfo> componentTypes, ChunkedWorldSnapshot& snapshot) const {
    if (impl_ == nullptr) {
        snapshot = {};
        snapshot.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        return;
    }
    impl_->CaptureChunkedSnapshot(componentTypes, snapshot);
}

void NativeArchetypeStorage::CaptureChunkedDeltaSnapshot(
    std::span<const ComponentTypeInfo> componentTypes,
    const ChunkedWorldSnapshot& baseline,
    ChunkedWorldDeltaSnapshot& delta) const {
    if (impl_ == nullptr) {
        delta = {};
        delta.componentTypes.assign(componentTypes.begin(), componentTypes.end());
        return;
    }
    impl_->CaptureChunkedDeltaSnapshot(componentTypes, baseline, delta);
}

bool NativeArchetypeStorage::StreamChunkedSnapshot(
    std::span<const ComponentTypeInfo> componentTypes,
    ChunkedWorldSnapshotChunkVisitor visitor,
    void* context) const {
    return impl_ != nullptr && impl_->StreamChunkedSnapshot(componentTypes, visitor, context);
}

std::uint64_t NativeArchetypeStorage::ArchetypeVersion(Entity entity) const {
    return impl_->ArchetypeVersion(entity);
}

std::uint64_t NativeArchetypeStorage::ComponentVersion(Entity entity, ComponentId componentId) const {
    return impl_->ComponentVersion(entity, componentId);
}

std::uint64_t NativeArchetypeStorage::ArchetypeComponentVersion(std::size_t archetypeIndex, ComponentId componentId) const {
    return impl_->ArchetypeComponentVersion(archetypeIndex, componentId);
}

std::size_t NativeArchetypeStorage::ChunkPayloadBytes() const noexcept {
    return impl_ != nullptr ? impl_->ChunkPayloadBytes() : 0;
}

std::size_t NativeArchetypeStorage::ChunkCount() const noexcept {
    return impl_ != nullptr ? impl_->ChunkCount() : 0;
}

NativeEcsStorageStats NativeArchetypeStorage::Stats() const {
    return impl_ != nullptr ? impl_->Stats() : NativeEcsStorageStats{};
}

NativeEcsMaintenanceStats NativeArchetypeStorage::MaintainChunks(NativeEcsMaintenanceBudget budget) {
    return impl_ != nullptr ? impl_->MaintainChunks(budget) : NativeEcsMaintenanceStats{};
}

} // namespace kb::ecs
