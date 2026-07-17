#include "save/SaveGameBinaryIO.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::save {
namespace {

[[nodiscard]] bool PrepareOutputPath(const std::filesystem::path& path) {
    if (!path.has_parent_path()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error;
}

[[nodiscard]] std::filesystem::path TempPathFor(const std::filesystem::path& path) {
    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    return tempPath;
}

[[nodiscard]] bool ReplaceFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

bool SaveGameBinaryIO::ByteReader::ReadRaw(void* out, std::size_t count) noexcept {
    if (count > bytes_.size() - cursor_ || cursor_ > bytes_.size()) {
        return false;
    }
    std::memcpy(out, bytes_.data() + cursor_, count);
    cursor_ += count;
    return true;
}

bool SaveGameBinaryIO::ByteReader::ReadUInt8(std::uint8_t& out) noexcept {
    return ReadRaw(&out, sizeof(out));
}

bool SaveGameBinaryIO::ByteReader::ReadUInt32(std::uint32_t& out) noexcept {
    return ReadRaw(&out, sizeof(out));
}

bool SaveGameBinaryIO::ByteReader::ReadUInt64(std::uint64_t& out) noexcept {
    return ReadRaw(&out, sizeof(out));
}

bool SaveGameBinaryIO::ByteReader::ReadInt64(std::int64_t& out) noexcept {
    return ReadRaw(&out, sizeof(out));
}

bool SaveGameBinaryIO::ByteReader::ReadDouble(double& out) noexcept {
    return ReadRaw(&out, sizeof(out));
}

bool SaveGameBinaryIO::ByteReader::ReadString(std::string& out, std::size_t maxBytes) noexcept {
    std::uint32_t length = 0;
    if (!ReadUInt32(length)) {
        return false;
    }
    if (length > maxBytes || length > bytes_.size() - cursor_) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes_.data() + cursor_), length);
    cursor_ += length;
    return true;
}

void SaveGameBinaryIO::WriteRaw(std::vector<std::uint8_t>& out, const void* data, std::size_t count) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + count);
}

void SaveGameBinaryIO::WriteUInt8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void SaveGameBinaryIO::WriteUInt32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    WriteRaw(out, &value, sizeof(value));
}

void SaveGameBinaryIO::WriteUInt64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    WriteRaw(out, &value, sizeof(value));
}

void SaveGameBinaryIO::WriteInt64(std::vector<std::uint8_t>& out, std::int64_t value) {
    WriteRaw(out, &value, sizeof(value));
}

void SaveGameBinaryIO::WriteDouble(std::vector<std::uint8_t>& out, double value) {
    WriteRaw(out, &value, sizeof(value));
}

void SaveGameBinaryIO::WriteString(std::vector<std::uint8_t>& out, std::string_view value) {
    WriteUInt32(out, static_cast<std::uint32_t>(value.size()));
    WriteRaw(out, value.data(), value.size());
}

bool SaveGameBinaryIO::ReadAllBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    return true;
}

bool SaveGameBinaryIO::WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (!PrepareOutputPath(path) || bytes.empty()) {
        return false;
    }

    const std::filesystem::path tempPath = TempPathFor(path);
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::error_code error;
            std::filesystem::remove(tempPath, error);
            return false;
        }
    }

    if (!ReplaceFileAtomically(tempPath, path)) {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

} // namespace kb::save
