#pragma once

#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace kb::input {

// One user-authored override: rebind the binding identified by `bindingId`
// (InputKeyMapping::bindingId, added in LIB-113 specifically so it could be
// addressed independently of which key it currently points to) to a
// different physical key / gamepad slot. Kept separate from the base
// InputMappingContextAsset so a content update to the shipped defaults never
// clobbers what the player rebound - the profile is applied ON TOP of the
// resolved base asset (see ApplyRebindProfile), not saved back into it.
struct InputRebindOverride {
    std::uint64_t bindingId = 0U;
    InputKey key = InputKey::None;
    std::uint8_t gamepadIndex = 0U;
};

// Identifies which OTHER binding a proposed rebind collides with.
struct InputRebindConflict {
    std::uint64_t conflictingBindingId = 0U;
};

// Scans every InputKeyMapping and InputCompositeSlot in `context` other than
// `bindingId` itself for the same (key, gamepadIndex) pair. Composite slots
// are checked - their key/gamepadIndex can legitimately collide with a plain
// mapping - even though individual slots are not yet independently rebindable
// by id (a composite has one bindingId for the whole group; addressing a
// single slot within it would need a (bindingId, slotIndex) pair, which
// ApplyRebind below does not yet support - a real, precise, documented scope
// boundary, not a silent gap).
[[nodiscard]] std::optional<InputRebindConflict> FindRebindConflict(
    const InputMappingContextAsset& context, std::uint64_t bindingId, InputKey newKey, std::uint8_t gamepadIndex) noexcept;

// Applies a rebind to the InputKeyMapping with the given bindingId. Returns
// false if no such binding exists, or if a real conflict was found and
// allowConflict is false (the default) - a caller that already surfaced the
// conflict to the user via FindRebindConflict and got an explicit "bind
// anyway" should pass allowConflict=true.
[[nodiscard]] bool ApplyRebind(InputMappingContextAsset& context, std::uint64_t bindingId, InputKey newKey,
                               std::uint8_t gamepadIndex, bool allowConflict = false) noexcept;

// Applies every override in `overrides` to `context` by bindingId, silently
// skipping any bindingId no longer present (the base asset may have changed
// since the profile was saved) - it does not re-validate conflicts, since a
// profile is assumed to have already passed validation when it was created.
void ApplyRebindProfile(InputMappingContextAsset& context, std::span<const InputRebindOverride> overrides);

// Binary round-trip for a rebind profile (LIB-119). Reuses InputAssetIO's
// magic/version/atomic-write conventions rather than inventing new I/O
// primitives. This is NOT the general user-settings/SaveGame system
// (versioned schema, apply/revert transactions, migration - that is
// LIB-162/163/217's explicit scope); it is the minimal, real persistence for
// exactly this override list.
[[nodiscard]] std::vector<std::uint8_t> EncodeRebindProfile(std::span<const InputRebindOverride> overrides);
[[nodiscard]] InputAssetLoadResult<std::vector<InputRebindOverride>> DecodeRebindProfile(std::span<const std::uint8_t> bytes);
[[nodiscard]] InputAssetLoadResult<std::vector<InputRebindOverride>> ReadRebindProfile(const std::filesystem::path& path);
[[nodiscard]] bool WriteRebindProfile(const std::filesystem::path& path, std::span<const InputRebindOverride> overrides);

} // namespace kb::input
