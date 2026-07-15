#include "engine/input/InputRebinding.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <array>
#include <cstddef>

namespace kb::input {
namespace {

// Reuse the engine's byte-level (de)serialization helpers; they are generic and
// not scene-specific despite their namespace.
namespace io = kb::scene::SceneAssetBinaryIO;

[[nodiscard]] bool ReadMagic(io::ByteReader& reader, const std::array<std::uint8_t, 8U>& expected) {
    std::array<std::uint8_t, 8U> magic{};
    if (!reader.ReadRaw(magic.data(), magic.size())) {
        return false;
    }
    return magic == expected;
}

// True if `key`/`gamepadIndex` match a mapping/slot other than `bindingId`
// itself. Shared by FindRebindConflict's two scan loops below.
[[nodiscard]] bool SameKeySlot(InputKey lhsKey, std::uint8_t lhsGamepad, InputKey rhsKey, std::uint8_t rhsGamepad) noexcept {
    return lhsKey != InputKey::None && lhsKey == rhsKey && lhsGamepad == rhsGamepad;
}

} // namespace

std::optional<InputRebindConflict> FindRebindConflict(
    const InputMappingContextAsset& context, std::uint64_t bindingId, InputKey newKey, std::uint8_t gamepadIndex) noexcept {
    for (const InputKeyMapping& mapping : context.mappings) {
        if (mapping.bindingId == bindingId) {
            continue;
        }
        if (SameKeySlot(newKey, gamepadIndex, mapping.key, mapping.gamepadIndex)) {
            return InputRebindConflict{.conflictingBindingId = mapping.bindingId};
        }
    }
    for (const InputCompositeBinding& composite : context.composites) {
        if (composite.bindingId == bindingId) {
            continue;
        }
        for (const InputCompositeSlot& slot : composite.slots) {
            if (SameKeySlot(newKey, gamepadIndex, slot.key, slot.gamepadIndex)) {
                return InputRebindConflict{.conflictingBindingId = composite.bindingId};
            }
        }
    }
    return std::nullopt;
}

bool ApplyRebind(InputMappingContextAsset& context, std::uint64_t bindingId, InputKey newKey,
                 std::uint8_t gamepadIndex, bool allowConflict) noexcept {
    InputKeyMapping* target = nullptr;
    for (InputKeyMapping& mapping : context.mappings) {
        if (mapping.bindingId == bindingId) {
            target = &mapping;
            break;
        }
    }
    if (target == nullptr) {
        return false;
    }
    if (!allowConflict && FindRebindConflict(context, bindingId, newKey, gamepadIndex).has_value()) {
        return false;
    }
    target->key = newKey;
    target->gamepadIndex = gamepadIndex;
    return true;
}

void ApplyRebindProfile(InputMappingContextAsset& context, std::span<const InputRebindOverride> overrides) {
    for (const InputRebindOverride& entry : overrides) {
        for (InputKeyMapping& mapping : context.mappings) {
            if (mapping.bindingId == entry.bindingId) {
                mapping.key = entry.key;
                mapping.gamepadIndex = entry.gamepadIndex;
                break;
            }
        }
    }
}

std::vector<std::uint8_t> EncodeRebindProfile(std::span<const InputRebindOverride> overrides) {
    std::vector<std::uint8_t> output;
    io::WriteRaw(output, InputAssetFormat::RebindProfileMagic.data(), InputAssetFormat::RebindProfileMagic.size());
    io::WriteUInt32(output, InputAssetFormat::BinaryVersion);
    io::WriteUInt32(output, static_cast<std::uint32_t>(overrides.size()));
    for (const InputRebindOverride& entry : overrides) {
        io::WriteUInt64(output, entry.bindingId);
        io::WriteUInt32(output, static_cast<std::uint32_t>(entry.key));
        io::WriteUInt8(output, entry.gamepadIndex);
    }
    return output;
}

InputAssetLoadResult<std::vector<InputRebindOverride>> DecodeRebindProfile(std::span<const std::uint8_t> bytes) {
    io::ByteReader reader(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!ReadMagic(reader, InputAssetFormat::RebindProfileMagic)) {
        return InputAssetLoadResult<std::vector<InputRebindOverride>>{
            .succeeded = false, .asset = {}, .error = "Invalid rebind profile magic"};
    }
    std::uint32_t version = 0U;
    if (!reader.ReadUInt32(version) || version != InputAssetFormat::BinaryVersion) {
        return InputAssetLoadResult<std::vector<InputRebindOverride>>{
            .succeeded = false, .asset = {}, .error = "Unsupported rebind profile version"};
    }

    std::uint32_t count = 0U;
    if (!reader.ReadUInt32(count) || count > InputAssetFormat::MaxRebindOverrideCount) {
        return InputAssetLoadResult<std::vector<InputRebindOverride>>{
            .succeeded = false, .asset = {}, .error = "Corrupt rebind profile payload"};
    }

    std::vector<InputRebindOverride> overrides;
    overrides.resize(count);
    for (InputRebindOverride& entry : overrides) {
        std::uint32_t key = 0U;
        if (!reader.ReadUInt64(entry.bindingId) || !reader.ReadUInt32(key) || !reader.ReadUInt8(entry.gamepadIndex)) {
            return InputAssetLoadResult<std::vector<InputRebindOverride>>{
                .succeeded = false, .asset = {}, .error = "Corrupt rebind profile entry"};
        }
        entry.key = static_cast<InputKey>(static_cast<std::uint16_t>(key));
    }
    return InputAssetLoadResult<std::vector<InputRebindOverride>>{.succeeded = true, .asset = std::move(overrides), .error = {}};
}

InputAssetLoadResult<std::vector<InputRebindOverride>> ReadRebindProfile(const std::filesystem::path& path) {
    return DecodeRebindProfile(io::ReadAllBytes(path));
}

bool WriteRebindProfile(const std::filesystem::path& path, std::span<const InputRebindOverride> overrides) {
    return io::WriteBytesAtomically(path, EncodeRebindProfile(overrides));
}

} // namespace kb::input
