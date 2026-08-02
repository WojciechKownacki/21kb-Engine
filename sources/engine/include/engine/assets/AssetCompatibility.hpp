#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

// LIB-159: why an asset (or something in its dependency closure) cannot be
// loaded in the current runtime.
enum class AssetCompatibilityIssue : std::uint8_t {
    // A declared dependency AssetId — or the validated asset itself — is not
    // registered in the AssetRegistry (the "missing dependency" the task
    // names). The offending id is the diagnostic's `dependency`.
    MissingDependency,
    // A registered asset's `type` has no loader registered in this runtime,
    // so it can never be loaded here (e.g. a "RenderMesh" asset validated in
    // a headless build with no renderer loaders). The asset is `asset`.
    IncompatibleType,
    // A registered dependency has the required broad asset type but violates
    // an asset-specific runtime contract, such as a skeleton signature.
    IncompatibleDependency,
};

[[nodiscard]] std::string_view ToString(AssetCompatibilityIssue issue) noexcept;

struct AssetCompatibilityDiagnostic {
    AssetCompatibilityIssue issue = AssetCompatibilityIssue::MissingDependency;
    // The asset that owns the problem (the parent that declared a missing
    // dependency, or the asset whose type has no loader).
    AssetId asset{};
    // The missing dependency id for MissingDependency; an invalid AssetId for
    // IncompatibleType.
    AssetId dependency{};
    // A ready-to-surface, human-readable explanation.
    std::string message;
};

struct AssetCompatibilityReport {
    // True exactly when `diagnostics` is empty.
    bool compatible = false;
    std::vector<AssetCompatibilityDiagnostic> diagnostics;

    // The diagnostics joined into a single newline-separated string (empty
    // when compatible) — the readable form the script Assets.Validate
    // surface and any tooling can present directly.
    [[nodiscard]] std::string FormatDiagnostics() const;
};

} // namespace kb::assets
