#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::save {

// LIB-162: self-contained little-endian byte reader/writer plus atomic file
// IO for the SaveGame format. Follows the per-subsystem convention (kb::scene
// and kb::project each own their own BinaryIO), keeping kb::save dependency-
// free rather than reaching into another subsystem's private headers.
class SaveGameBinaryIO {
public:
    // Bounds-checked sequential reader over a byte buffer. Every Read* returns
    // false (and leaves the output untouched, cursor unmoved) if the buffer
    // does not hold enough bytes, so a truncated/hostile file can never read
    // out of bounds.
    class ByteReader {
    public:
        explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept
            : bytes_(bytes) {}

        [[nodiscard]] bool ReadRaw(void* out, std::size_t count) noexcept;
        [[nodiscard]] bool ReadUInt8(std::uint8_t& out) noexcept;
        [[nodiscard]] bool ReadUInt32(std::uint32_t& out) noexcept;
        [[nodiscard]] bool ReadUInt64(std::uint64_t& out) noexcept;
        [[nodiscard]] bool ReadInt64(std::int64_t& out) noexcept;
        [[nodiscard]] bool ReadDouble(double& out) noexcept;
        // Reads a uint32 length prefix then that many bytes; rejects a length
        // above `maxBytes` or beyond the buffer.
        [[nodiscard]] bool ReadString(std::string& out, std::size_t maxBytes) noexcept;
        [[nodiscard]] bool Exhausted() const noexcept {
            return cursor_ >= bytes_.size();
        }

    private:
        std::span<const std::uint8_t> bytes_;
        std::size_t cursor_ = 0;
    };

    static void WriteRaw(std::vector<std::uint8_t>& out, const void* data, std::size_t count);
    static void WriteUInt8(std::vector<std::uint8_t>& out, std::uint8_t value);
    static void WriteUInt32(std::vector<std::uint8_t>& out, std::uint32_t value);
    static void WriteUInt64(std::vector<std::uint8_t>& out, std::uint64_t value);
    static void WriteInt64(std::vector<std::uint8_t>& out, std::int64_t value);
    static void WriteDouble(std::vector<std::uint8_t>& out, double value);
    static void WriteString(std::vector<std::uint8_t>& out, std::string_view value);

    // Reads the whole file into memory; returns false if it does not exist or
    // cannot be read.
    [[nodiscard]] static bool ReadAllBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out);
    // Writes `bytes` to `path` atomically: creates parent dirs, writes a
    // sibling ".tmp", then replaces the target in one step (MoveFileEx with
    // REPLACE_EXISTING|WRITE_THROUGH on Windows, rename on POSIX). Returns
    // false on any IO failure (and leaves any previous file at `path`
    // untouched). Rejects an empty payload — a valid SaveGame always has a
    // header.
    [[nodiscard]] static bool WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
};

} // namespace kb::save
