#pragma once

#include "scene/asset/io/SceneAssetFormat.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::scene::SceneAssetBinaryIO {

[[nodiscard]] std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path);
[[nodiscard]] bool WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

class ByteReader {
public:
    explicit ByteReader(std::vector<std::uint8_t> bytes) noexcept
        : bytes_(std::move(bytes)) {}

    [[nodiscard]] bool ReadRaw(void* output, std::size_t size) {
        if (Remaining() < size) {
            return false;
        }
        std::memcpy(output, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    [[nodiscard]] bool ReadUInt8(std::uint8_t& output) {
        if (Remaining() < 1U) {
            return false;
        }
        output = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool ReadBool(bool& output) {
        std::uint8_t value = 0U;
        if (!ReadUInt8(value) || value > 1U) {
            return false;
        }
        output = value != 0U;
        return true;
    }

    [[nodiscard]] bool ReadUInt32(std::uint32_t& output) {
        if (Remaining() < sizeof(std::uint32_t)) {
            return false;
        }
        output = static_cast<std::uint32_t>(bytes_[offset_]) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
        offset_ += sizeof(std::uint32_t);
        return true;
    }

    [[nodiscard]] bool ReadUInt64(std::uint64_t& output) {
        if (Remaining() < sizeof(std::uint64_t)) {
            return false;
        }
        output = 0U;
        for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
            output |= static_cast<std::uint64_t>(bytes_[offset_ + byte]) << (byte * 8U);
        }
        offset_ += sizeof(std::uint64_t);
        return true;
    }

    [[nodiscard]] bool ReadFloat(float& output) {
        std::uint32_t bits = 0U;
        if (!ReadUInt32(bits)) {
            return false;
        }
        output = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool ReadString(std::string& output, std::uint32_t maxBytes = SceneAssetFormat::MaxStringBytes) {
        std::uint32_t length = 0U;
        if (!ReadUInt32(length) || length > maxBytes || Remaining() < length) {
            return false;
        }
        output.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    [[nodiscard]] bool Exhausted() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    [[nodiscard]] std::size_t Remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    std::vector<std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
};

inline void WriteRaw(std::vector<std::uint8_t>& output, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

inline void WriteUInt8(std::vector<std::uint8_t>& output, std::uint8_t value) {
    output.push_back(value);
}

inline void WriteBool(std::vector<std::uint8_t>& output, bool value) {
    WriteUInt8(output, value ? 1U : 0U);
}

inline void WriteUInt32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

inline void WriteUInt64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

inline void WriteFloat(std::vector<std::uint8_t>& output, float value) {
    WriteUInt32(output, std::bit_cast<std::uint32_t>(value));
}

inline void WriteString(std::vector<std::uint8_t>& output, std::string_view value) {
    WriteUInt32(output, static_cast<std::uint32_t>(value.size()));
    WriteRaw(output, value.data(), value.size());
}

} // namespace kb::scene::SceneAssetBinaryIO
