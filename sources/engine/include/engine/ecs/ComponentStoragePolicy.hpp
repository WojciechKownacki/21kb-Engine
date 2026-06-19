#pragma once

namespace kb::ecs {

enum class ComponentStorageClass {
    HotTable,
    ColdTable,
    SparseTag,
    SparsePayload,
    SharedValue,
    ExternalBlob,
};

struct ComponentRegistrationOptions {
    ComponentStorageClass storageClass = ComponentStorageClass::HotTable;
};

[[nodiscard]] constexpr bool IsArchetypeTableStorage(ComponentStorageClass storageClass) noexcept {
    return storageClass == ComponentStorageClass::HotTable || storageClass == ComponentStorageClass::ColdTable;
}

[[nodiscard]] constexpr bool UsesHotChunkPayload(ComponentStorageClass storageClass) noexcept {
    return storageClass == ComponentStorageClass::HotTable;
}

[[nodiscard]] constexpr bool UsesNativeSideStorage(ComponentStorageClass storageClass) noexcept {
    return storageClass != ComponentStorageClass::HotTable;
}

[[nodiscard]] constexpr bool IsSparseStorage(ComponentStorageClass storageClass) noexcept {
    return storageClass == ComponentStorageClass::SparseTag || storageClass == ComponentStorageClass::SparsePayload;
}

} // namespace kb::ecs
