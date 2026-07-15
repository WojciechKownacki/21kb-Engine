#pragma once

#include "engine/math/EngineMath.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace kb::library {

// LIB-063: parsing for the four data shapes the plan calls out — numbers,
// GUID, colors, dates — all with "unambiguous locale/invariant culture"
// behaviour: none of these functions consult the current C locale (no
// std::atof/std::strtod/std::stringstream against the PROCESS-GLOBAL
// locale, whose behaviour depends on it — e.g. ',' vs '.' as the decimal
// separator, which would make the SAME input string parse differently on
// different machines/configurations). Integers go through std::from_chars
// (C++17), which the standard defines as locale-independent by
// construction, not merely "usually is". Doubles use a hand-scoped "C"
// locale (strtod_l/_strtod_l, never touching the process-global locale) —
// std::from_chars<double> would be the natural first choice, but Apple's
// shipped libc++ does not implement it (confirmed empirically: "call to
// deleted function", not merely gated behind a deployment-target
// availability macro), so EngineLibraryParsing.cpp works around the gap
// while preserving the exact same locale-independence guarantee a
// different way. GUID/Color/Date are hand-validated against one fixed,
// documented textual grammar (no format auto-detection, no alternate
// spellings) for the same reason: one unambiguous format, everywhere,
// rather than a locale-sensitive guess.
//
// Every function follows the established TryParse* contract already used
// elsewhere in this codebase (e.g. TryParseVisualGraphValueType,
// ScriptAssetLoader::TryParseScriptValueType): returns bool, writes the
// result only on success, never throws, never partially writes on
// failure.

[[nodiscard]] bool TryParseInt64(std::string_view text, std::int64_t& outValue) noexcept;
[[nodiscard]] bool TryParseUInt64(std::string_view text, std::uint64_t& outValue) noexcept;
[[nodiscard]] bool TryParseDouble(std::string_view text, double& outValue) noexcept;

// Canonical 8-4-4-4-12 hyphenated hex form only (e.g.
// "3F2504E0-4F89-11D3-9A0C-0305E82C3301"), 36 characters, hex digits
// case-insensitive, hyphens fixed at positions 8/13/18/23 — the one
// unambiguous textual GUID grammar. Pure validation (kb::script::ScriptValue
// already stores a Guid as its raw string via LIB-041; this closes the
// "Guid format validation" deferral that ScriptValue.hpp's constructor
// comment explicitly left to this task).
[[nodiscard]] bool TryParseGuid(std::string_view text) noexcept;

// "#RRGGBB" or "#RRGGBBAA" — hex, case-insensitive, no alpha defaults to
// fully opaque (matching kb::math::Color's own default of a=1). Each pair
// maps 0-255 to a linear-space [0,1] channel value by dividing by 255,
// consistent with kb::math::Color's documented range.
[[nodiscard]] bool TryParseColor(std::string_view text, kb::math::Color& outColor) noexcept;

// "YYYY-MM-DD" (ISO 8601 calendar date, the one unambiguous date grammar —
// never "MM/DD/YYYY" or "DD.MM.YYYY", which are locale conventions, not a
// single invariant format). Reuses std::chrono::year_month_day rather than
// a hand-rolled date struct — its .ok() already implements real calendar
// validity (leap years, days-per-month) correctly, so a syntactically
// well-formed but calendrically invalid date like "2023-02-30" is rejected
// by the standard library's own logic, not reimplemented here.
[[nodiscard]] bool TryParseDate(std::string_view text, std::chrono::year_month_day& outDate) noexcept;

} // namespace kb::library
