#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
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

[[nodiscard]] Entity PackEntity(std::uint32_t index, std::uint32_t generation) noexcept {
    return Entity{ (static_cast<Entity::IdType>(generation) << 32U) | (static_cast<Entity::IdType>(index) + 1U) };
}

[[nodiscard]] std::uint32_t EntityIndex(Entity entity) noexcept {
    const Entity::IdType packedIndex = entity.Id() & 0xFFFFFFFFULL;
    return packedIndex == 0 ? kInvalidEntityIndex : static_cast<std::uint32_t>(packedIndex - 1U);
}

[[nodiscard]] std::uint32_t EntityGeneration(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity.Id() >> 32U);
}

void ValidateComponentType(const NativeComponentType& type) {
    if (type.id == 0 || type.size == 0 || type.alignment == 0 || !std::has_single_bit(type.alignment)) {
        throw std::invalid_argument("Invalid native ECS component type");
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
        if (!freeList_.empty()) {
            std::byte* block = freeList_.back();
            freeList_.pop_back();
            ++chunksInUse_;
            return block;
        }
        ++chunksInUse_;
        return static_cast<std::byte*>(::operator new(payloadBytes_, std::align_val_t{ alignment_ }));
    }

    void Release(std::byte* block) noexcept {
        if (block == nullptr) {
            return;
        }
        --chunksInUse_;
        freeList_.push_back(block);
    }

    [[nodiscard]] std::size_t PayloadBytes() const noexcept { return payloadBytes_; }
    [[nodiscard]] std::size_t ChunksInUse() const noexcept { return chunksInUse_; }

private:
    std::size_t payloadBytes_ = 0;
    std::size_t alignment_ = kChunkAlignment;
    std::size_t chunksInUse_ = 0;
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
    std::uint32_t generation = 1;
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
            blocks_.resize(block + 1U);
        }
        blocks_[block] |= std::uint64_t{ 1 } << (bitIndex % kBitsPerBlock);
    }

    [[nodiscard]] bool Has(std::size_t bitIndex) const noexcept {
        const std::size_t block = bitIndex / kBitsPerBlock;
        return block < blocks_.size() && (blocks_[block] & (std::uint64_t{ 1 } << (bitIndex % kBitsPerBlock))) != 0;
    }

    [[nodiscard]] bool ContainsAll(const ComponentSignature& required) const noexcept {
        for (std::size_t index = 0; index < required.blocks_.size(); ++index) {
            const std::uint64_t available = index < blocks_.size() ? blocks_[index] : 0U;
            if ((available & required.blocks_[index]) != required.blocks_[index]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool operator==(const ComponentSignature& other) const noexcept {
        return blocks_ == other.blocks_;
    }

private:
    static constexpr std::size_t kBitsPerBlock = 64;

    std::vector<std::uint64_t> blocks_;
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

    void WriteComponent(EntityLocation location, ComponentId componentId, const void* data, std::size_t size) {
        const ComponentLayout* column = FindColumn(componentId);
        if (column == nullptr || column->type.size != size || data == nullptr) {
            throw std::invalid_argument("Invalid native ECS component write");
        }
        std::memcpy(ComponentData(location, componentId), data, size);
        TouchComponent(componentId);
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
        return chunks_[location.chunk].payload.Data() + column.offset + (location.row * column.type.size);
    }

    [[nodiscard]] const void* ComponentData(EntityLocation location, const ComponentLayout& column) const {
        return chunks_[location.chunk].payload.Data() + column.offset + (location.row * column.type.size);
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

    [[nodiscard]] bool IsAlive(Entity entity) const noexcept {
        const auto found = entitySlots_.find(entity.Id());
        if (found == entitySlots_.end() || found->second >= records_.size()) {
            return false;
        }
        const EntityRecord& record = records_[found->second];
        return record.alive && record.entity == entity && record.generation == EntityGeneration(entity);
    }

    void AddComponents(Entity entity, std::span<const NativeComponentValue> components) {
        if (components.empty()) {
            return;
        }
        const std::vector<NativeComponentType> addedTypes = NormalizeTypes(components);
        EntityRecord& record = LiveRecord(entity);
        const std::size_t sourceIndex = record.location.table;
        const ArchetypeTable& source = tables_[sourceIndex];
        std::vector<NativeComponentType> targetTypes(source.Types().begin(), source.Types().end());
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

    void RemoveComponents(Entity entity, std::span<const ComponentId> componentIds) {
        if (componentIds.empty()) {
            return;
        }
        const std::vector<ComponentId> removedIds = NormalizeComponentIds(componentIds);
        EntityRecord& record = LiveRecord(entity);
        const std::size_t sourceIndex = record.location.table;
        const ArchetypeTable& source = tables_[sourceIndex];
        std::vector<NativeComponentType> targetTypes;
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

    void SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size) {
        EntityRecord& record = LiveRecord(entity);
        tables_[record.location.table].WriteComponent(record.location, componentId, data, size);
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

        const std::vector<ComponentId> queryIds = NormalizeComponentIds(componentIds);
        std::vector<ComponentId> requiredIds = NormalizeComponentIds(requiredComponentIds);
        requiredIds.insert(requiredIds.end(), queryIds.begin(), queryIds.end());
        std::sort(requiredIds.begin(), requiredIds.end());
        requiredIds.erase(std::unique(requiredIds.begin(), requiredIds.end()), requiredIds.end());

        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds);
        if (!requiredSignature.has_value()) {
            return;
        }
        const std::vector<ComponentId> excludedIds = NormalizeComponentIds(excludedComponentIds);

        std::size_t sequence = 0;
        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            const ArchetypeTable& table = tables_[tableIndex];
            if (!table.Matches(*requiredSignature) || HasExcludedComponent(table, excludedIds)) {
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

        const std::vector<ComponentId> queryIds = NormalizeComponentIds(componentIds);
        std::vector<ComponentId> requiredIds = NormalizeComponentIds(requiredComponentIds);
        requiredIds.insert(requiredIds.end(), queryIds.begin(), queryIds.end());
        std::sort(requiredIds.begin(), requiredIds.end());
        requiredIds.erase(std::unique(requiredIds.begin(), requiredIds.end()), requiredIds.end());

        const std::optional<ComponentSignature> requiredSignature = signatureRegistry_.TryBuild(requiredIds);
        if (!requiredSignature.has_value()) {
            return;
        }
        const std::vector<ComponentId> excludedIds = NormalizeComponentIds(excludedComponentIds);

        std::size_t sequence = 0;
        for (std::size_t tableIndex = 0; tableIndex < tables_.size(); ++tableIndex) {
            ArchetypeTable& table = tables_[tableIndex];
            if (!table.Matches(*requiredSignature) || HasExcludedComponent(table, excludedIds)) {
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

    [[nodiscard]] NativeEcsStorageStats Stats() const noexcept {
        NativeEcsStorageStats stats;
        stats.chunks = pool_.ChunksInUse();
        stats.archetypeCount = tables_.size();
        stats.liveEntities = liveEntities_;
        for (const ArchetypeTable& table : tables_) {
            stats.usedBytes += table.UsedBytes();
        }
        stats.wastedBytes = (stats.chunks * chunkPayloadBytes_) - std::min(stats.usedBytes, stats.chunks * chunkPayloadBytes_);
        return stats;
    }

private:
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
            entitySlots_[record.entity.Id()] = index;
            return record.entity;
        }

        if (records_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("Native ECS entity capacity exceeded");
        }
        const std::uint32_t index = static_cast<std::uint32_t>(records_.size());
        Entity entity = PackEntity(index, 1U);
        records_.push_back(EntityRecord{
            .generation = 1U,
            .alive = true,
            .ownsGeneratedId = true,
            .entity = entity,
        });
        entitySlots_[entity.Id()] = index;
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
    ComponentSignatureRegistry signatureRegistry_;
    std::vector<ArchetypeTable> tables_;
    std::size_t liveEntities_ = 0;
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

void NativeArchetypeStorage::AdoptEntity(Entity entity, std::span<const NativeComponentValue> components) {
    impl_->AdoptEntity(entity, components);
}

void NativeArchetypeStorage::DestroyEntity(Entity entity) {
    impl_->DestroyEntity(entity);
}

bool NativeArchetypeStorage::IsAlive(Entity entity) const noexcept {
    return impl_ != nullptr && impl_->IsAlive(entity);
}

void NativeArchetypeStorage::AddComponents(Entity entity, std::span<const NativeComponentValue> components) {
    impl_->AddComponents(entity, components);
}

void NativeArchetypeStorage::RemoveComponents(Entity entity, std::span<const ComponentId> componentIds) {
    impl_->RemoveComponents(entity, componentIds);
}

void NativeArchetypeStorage::SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size) {
    impl_->SetComponent(entity, componentId, data, size);
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

NativeEcsStorageStats NativeArchetypeStorage::Stats() const noexcept {
    return impl_ != nullptr ? impl_->Stats() : NativeEcsStorageStats{};
}

} // namespace kb::ecs
