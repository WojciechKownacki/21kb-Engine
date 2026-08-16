#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::assets {
struct AssetMetadata;
class AssetRegistry;
} // namespace kb::assets

namespace kb::scene {

enum class ParticleEffectDiagnosticSeverity : std::uint8_t { Warning, Error };
enum class ParticleEffectDiagnosticCode : std::uint16_t {
    FileNotFound,
    FileAccessFailed,
    SourceTooLarge,
    EmptySource,
    InvalidHeader,
    UnsupportedVersion,
    InvalidUtf8,
    InvalidSyntax,
    InvalidEscape,
    DuplicateKey,
    UnknownKey,
    MissingKey,
    InvalidValue,
    InvalidEnum,
    CountMismatch,
    LimitExceeded,
    InvalidStableId,
    UnsortedStableId,
    DuplicateModule,
    InvalidReference,
    MissingDependency,
    MismatchedReference,
    WrongAssetType,
    CyclicReference,
    InvalidCurve,
    InvalidGradient,
    AtomicWriteFailed,
    UnsupportedCapability,
    InvalidCompiledCache,
};

struct ParticleEffectDiagnostic {
    ParticleEffectDiagnosticCode code = ParticleEffectDiagnosticCode::InvalidValue;
    ParticleEffectDiagnosticSeverity severity = ParticleEffectDiagnosticSeverity::Error;
    std::uint32_t line = 0U;
    std::string propertyPath;
    ParticleStableId emitterId = 0U;
    ParticleStableId moduleId = 0U;
    std::string message;
};

struct ParticleEffectValidationResult {
    std::vector<ParticleEffectDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct ParticleEffectDependencyResult {
    std::vector<kb::assets::AssetId> dependencies;
    std::vector<kb::assets::AssetId> transitiveDependencies;
    std::vector<ParticleEffectDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

class ParticleEffectAssetValidator final {
  public:
    ParticleEffectAssetValidator() = delete;
    [[nodiscard]] static ParticleEffectValidationResult ValidateStructure(const ParticleEffectAsset& asset);
    [[nodiscard]] static ParticleEffectDependencyResult ValidateDependencies(const kb::assets::AssetMetadata& metadata,
                                                                             const kb::assets::AssetRegistry& registry);
    [[nodiscard]] static ParticleEffectDependencyResult ValidateDependencies(const ParticleEffectAsset& workingAsset,
                                                                             const kb::assets::AssetMetadata& metadata,
                                                                             const kb::assets::AssetRegistry& registry);
};

[[nodiscard]] std::string FormatParticleEffectDiagnostic(const ParticleEffectDiagnostic& diagnostic);

} // namespace kb::scene
