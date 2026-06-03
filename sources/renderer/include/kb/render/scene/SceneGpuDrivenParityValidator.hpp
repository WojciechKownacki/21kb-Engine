#pragma once

#include <cstdint>
#include <span>

namespace kb::render {

enum class SceneGpuDrivenParityValidationStatus : std::uint8_t {
    Valid,
    CpuRecordMissing,
    GpuRecordMissing,
    VisibilityMismatch,
    LodMismatch,
    MeshletRangeMismatch,
    DroppedInstanceMismatch,
    DroppedInstanceBudgetExceeded,
};

struct SceneGpuDrivenInstanceValidationRecord {
    std::uint64_t entityId = 0;
    std::uint8_t lodLevel = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;
    bool visible = false;
    bool dropped = false;

    [[nodiscard]] constexpr bool HasValidEntity() const noexcept {
        return entityId != 0U;
    }
};

struct SceneGpuDrivenParityValidationDesc {
    std::span<const SceneGpuDrivenInstanceValidationRecord> cpuRecords{};
    std::span<const SceneGpuDrivenInstanceValidationRecord> gpuRecords{};
    std::uint32_t droppedInstanceBudget = 0;
    bool validateVisibility = true;
    bool validateLod = true;
    bool validateMeshletRange = true;
    bool validateDroppedInstances = true;
};

struct SceneGpuDrivenParityValidationResult {
    SceneGpuDrivenParityValidationStatus status = SceneGpuDrivenParityValidationStatus::Valid;
    std::uint64_t entityId = 0;
    std::uint32_t cpuDroppedInstanceCount = 0;
    std::uint32_t gpuDroppedInstanceCount = 0;
    std::uint8_t cpuLodLevel = 0;
    std::uint8_t gpuLodLevel = 0;
    std::uint32_t cpuFirstMeshlet = 0;
    std::uint32_t gpuFirstMeshlet = 0;
    std::uint32_t cpuMeshletCount = 0;
    std::uint32_t gpuMeshletCount = 0;
    bool cpuVisible = false;
    bool gpuVisible = false;
    bool cpuDropped = false;
    bool gpuDropped = false;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return status == SceneGpuDrivenParityValidationStatus::Valid;
    }
};

class SceneGpuDrivenParityValidator {
public:
    SceneGpuDrivenParityValidator() = delete;

    [[nodiscard]] static SceneGpuDrivenParityValidationResult Validate(
        const SceneGpuDrivenParityValidationDesc& desc) noexcept {
        SceneGpuDrivenParityValidationResult result{};

        for (const SceneGpuDrivenInstanceValidationRecord& cpuRecord : desc.cpuRecords) {
            if (!cpuRecord.HasValidEntity()) {
                continue;
            }

            const SceneGpuDrivenInstanceValidationRecord* gpuRecord = FindByEntity(desc.gpuRecords, cpuRecord.entityId);
            if (gpuRecord == nullptr) {
                return Failure(SceneGpuDrivenParityValidationStatus::GpuRecordMissing, cpuRecord, {});
            }

            if (desc.validateVisibility && cpuRecord.visible != gpuRecord->visible) {
                return Failure(SceneGpuDrivenParityValidationStatus::VisibilityMismatch, cpuRecord, *gpuRecord);
            }
            if (desc.validateLod && cpuRecord.lodLevel != gpuRecord->lodLevel) {
                return Failure(SceneGpuDrivenParityValidationStatus::LodMismatch, cpuRecord, *gpuRecord);
            }
            if (desc.validateMeshletRange &&
                (cpuRecord.firstMeshlet != gpuRecord->firstMeshlet || cpuRecord.meshletCount != gpuRecord->meshletCount)) {
                return Failure(SceneGpuDrivenParityValidationStatus::MeshletRangeMismatch, cpuRecord, *gpuRecord);
            }
            if (desc.validateDroppedInstances && cpuRecord.dropped != gpuRecord->dropped) {
                return Failure(SceneGpuDrivenParityValidationStatus::DroppedInstanceMismatch, cpuRecord, *gpuRecord);
            }
        }

        for (const SceneGpuDrivenInstanceValidationRecord& gpuRecord : desc.gpuRecords) {
            if (!gpuRecord.HasValidEntity()) {
                continue;
            }
            if (FindByEntity(desc.cpuRecords, gpuRecord.entityId) == nullptr) {
                return Failure(SceneGpuDrivenParityValidationStatus::CpuRecordMissing, {}, gpuRecord);
            }
        }

        result.cpuDroppedInstanceCount = CountDropped(desc.cpuRecords);
        result.gpuDroppedInstanceCount = CountDropped(desc.gpuRecords);
        if (desc.validateDroppedInstances && desc.droppedInstanceBudget != 0U &&
            result.gpuDroppedInstanceCount > desc.droppedInstanceBudget) {
            result.status = SceneGpuDrivenParityValidationStatus::DroppedInstanceBudgetExceeded;
        }
        return result;
    }

private:
    [[nodiscard]] static const SceneGpuDrivenInstanceValidationRecord* FindByEntity(
        std::span<const SceneGpuDrivenInstanceValidationRecord> records,
        std::uint64_t entityId) noexcept {
        for (const SceneGpuDrivenInstanceValidationRecord& record : records) {
            if (record.entityId == entityId) {
                return &record;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static std::uint32_t CountDropped(
        std::span<const SceneGpuDrivenInstanceValidationRecord> records) noexcept {
        std::uint32_t count = 0U;
        for (const SceneGpuDrivenInstanceValidationRecord& record : records) {
            count += record.dropped ? 1U : 0U;
        }
        return count;
    }

    [[nodiscard]] static constexpr SceneGpuDrivenParityValidationResult Failure(
        SceneGpuDrivenParityValidationStatus status,
        SceneGpuDrivenInstanceValidationRecord cpuRecord,
        SceneGpuDrivenInstanceValidationRecord gpuRecord) noexcept {
        return SceneGpuDrivenParityValidationResult{
            .status = status,
            .entityId = cpuRecord.HasValidEntity() ? cpuRecord.entityId : gpuRecord.entityId,
            .cpuDroppedInstanceCount = cpuRecord.dropped ? 1U : 0U,
            .gpuDroppedInstanceCount = gpuRecord.dropped ? 1U : 0U,
            .cpuLodLevel = cpuRecord.lodLevel,
            .gpuLodLevel = gpuRecord.lodLevel,
            .cpuFirstMeshlet = cpuRecord.firstMeshlet,
            .gpuFirstMeshlet = gpuRecord.firstMeshlet,
            .cpuMeshletCount = cpuRecord.meshletCount,
            .gpuMeshletCount = gpuRecord.meshletCount,
            .cpuVisible = cpuRecord.visible,
            .gpuVisible = gpuRecord.visible,
            .cpuDropped = cpuRecord.dropped,
            .gpuDropped = gpuRecord.dropped,
        };
    }
};

} // namespace kb::render
