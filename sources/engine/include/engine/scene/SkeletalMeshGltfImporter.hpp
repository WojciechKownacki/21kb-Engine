#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

struct SkeletalMeshGltfImportResult {
    SkeletonAsset skeleton;
    SkeletalMeshAsset mesh;
    std::vector<AnimationClip> clips;
};

enum class SkeletalMeshGltfImportDiagnosticSeverity : std::uint8_t {
    Warning,
    Error,
};

struct SkeletalMeshGltfImportDiagnostic {
    SkeletalMeshGltfImportDiagnosticSeverity severity = SkeletalMeshGltfImportDiagnosticSeverity::Error;
    std::string message;
    std::filesystem::path sourcePath;
    std::string mesh;
    std::string node;
    std::string bone;
    std::int32_t primitiveIndex = -1;
    std::int32_t channelIndex = -1;
};

struct SkeletalMeshGltfImportReport {
    std::vector<SkeletalMeshGltfImportDiagnostic> diagnostics;

    [[nodiscard]] bool HasErrors() const noexcept;
};

enum class SkeletalMeshGltfAxis : std::uint8_t {
    X = 0U,
    Y = 1U,
    Z = 2U,
};

// glTF authors coordinates in right-handed metres. The default maps that
// convention into the engine's left-handed, Y-up metre convention by
// reflecting Z. `engineAxes` and `engineAxisSigns` describe each engine
// component in source coordinates, so both axis order and handedness are
// explicit and reviewable at the import boundary.
struct SkeletalMeshGltfCoordinateConversion {
    std::array<SkeletalMeshGltfAxis, 3U> engineAxes{
        SkeletalMeshGltfAxis::X, SkeletalMeshGltfAxis::Y, SkeletalMeshGltfAxis::Z,
    };
    std::array<float, 3U> engineAxisSigns{ 1.0F, 1.0F, -1.0F };
    float unitScale = 1.0F;
};

using SkeletalMeshGltfMaterialResolver = std::uint64_t (*)(
    std::string_view sourceMaterialName,
    void* userData);

struct SkeletalMeshGltfImportOptions {
    SkeletalMeshGltfCoordinateConversion coordinateConversion{};
    // A primitive with a glTF material is rejected unless this resolver
    // returns a canonical Material asset id. This prevents a source material
    // from being silently discarded during skeletal import.
    SkeletalMeshGltfMaterialResolver materialResolver = nullptr;
    void* materialResolverUserData = nullptr;
};

// Imports one glTF skin and the mesh node bound to it into the canonical
// skeletal asset pair. The caller owns atomic publication of the result.
class SkeletalMeshGltfImporter final {
public:
    SkeletalMeshGltfImporter() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshGltfImportResult> Import(
        const std::filesystem::path& path,
        std::uint64_t skeletonAssetId,
        const SkeletalMeshGltfImportOptions& options,
        std::string* error = nullptr,
        SkeletalMeshGltfImportReport* report = nullptr);

    [[nodiscard]] static std::optional<SkeletalMeshGltfImportResult> Import(
        const std::filesystem::path& path,
        std::uint64_t skeletonAssetId,
        std::string* error = nullptr,
        SkeletalMeshGltfImportReport* report = nullptr) {
        return Import(path, skeletonAssetId, {}, error, report);
    }
};

} // namespace kb::scene
