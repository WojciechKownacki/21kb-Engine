#pragma once

#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace kb::input {

// Binary format constants for input assets. Magic / version are part of the
// on-disk format and must stay stable.
struct InputAssetFormat {
    static constexpr std::array<std::uint8_t, 8U> ActionMagic{ '2', '1', 'K', 'B', 'I', 'A', 'C', 0 };
    static constexpr std::array<std::uint8_t, 8U> ContextMagic{ '2', '1', 'K', 'B', 'I', 'M', 'C', 0 };
    static constexpr std::uint32_t BinaryVersion = 1U;
    static constexpr std::uint32_t MaxMappingCount = 100'000U;
    static constexpr std::uint32_t MaxStackCount = 64U; // modifiers/triggers per mapping
    static constexpr std::uint32_t MaxNameBytes = 1U << 16U;
    static constexpr std::string_view ActionExtension = ".21kbinputaction";
    // An Input Axis is an Input Action whose value type is an analog axis; it
    // shares the action binary format + loader, distinguished by file extension.
    static constexpr std::string_view AxisExtension = ".21kbinputaxis";
    static constexpr std::string_view ContextExtension = ".21kbinputcontext";
};

template <typename T>
struct InputAssetLoadResult {
    bool succeeded = false;
    T asset{};
    std::string error;
};

// --- InputAction ---
[[nodiscard]] std::vector<std::uint8_t> EncodeInputAction(const InputActionAsset& asset);
[[nodiscard]] InputAssetLoadResult<InputActionAsset> DecodeInputAction(std::span<const std::uint8_t> bytes);
[[nodiscard]] InputAssetLoadResult<InputActionAsset> ReadInputAction(const std::filesystem::path& path);
[[nodiscard]] bool WriteInputAction(const std::filesystem::path& path, const InputActionAsset& asset);

// --- InputMappingContext ---
[[nodiscard]] std::vector<std::uint8_t> EncodeInputMappingContext(const InputMappingContextAsset& asset);
[[nodiscard]] InputAssetLoadResult<InputMappingContextAsset> DecodeInputMappingContext(std::span<const std::uint8_t> bytes);
[[nodiscard]] InputAssetLoadResult<InputMappingContextAsset> ReadInputMappingContext(const std::filesystem::path& path);
[[nodiscard]] bool WriteInputMappingContext(const std::filesystem::path& path, const InputMappingContextAsset& asset);

} // namespace kb::input
