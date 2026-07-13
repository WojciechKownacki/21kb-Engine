#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::library {

enum class ApiChangeSeverity : std::uint8_t {
    // A function/component/property/lifecycle event present in the
    // baseline is missing, or present in both but with an incompatible
    // contract (inputs/outputs pin name, type, or required-ness changed).
    // A Lua script or Visual Graph asset built against the baseline can
    // fail to load or run against `current`.
    Breaking,
    // Only added in `current`; nothing that worked against the baseline
    // stops working.
    Additive,
};

struct ApiChange {
    ApiChangeSeverity severity = ApiChangeSeverity::Additive;
    std::string description;
};

struct ApiCompatibilityReport {
    std::vector<ApiChange> changes;

    [[nodiscard]] bool HasBreakingChanges() const noexcept;
};

// Compares two ScriptApiCatalog snapshots — typically a stored baseline
// from a previous build and the catalog the current build just registered
// — and classifies every difference. LIB-021's registry lock prevents a
// function's contract from changing silently within one running world;
// this compares across two SEPARATE builds, which LIB-021 does not cover
// and which is what LIB-024's CI compatibility check needs.
[[nodiscard]] ApiCompatibilityReport CompareApiCatalogs(
    const kb::script::ScriptApiCatalog& baseline,
    const kb::script::ScriptApiCatalog& current);

} // namespace kb::library
