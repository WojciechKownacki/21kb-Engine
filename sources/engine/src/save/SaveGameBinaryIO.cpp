#include "save/SaveGameBinaryIO.hpp"

#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <io.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <unistd.h>
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

bool SaveGameBinaryIO::ReadAllBytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& out,
    std::size_t maxBytes,
    bool& tooLarge) {
    tooLarge = false;
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    if (!input.is_open()) {
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        return false;
    }
    const std::uintmax_t size = static_cast<std::uintmax_t>(end);
    if (size > maxBytes) {
        tooLarge = true;
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (input.gcount() != static_cast<std::streamsize>(out.size())) {
            out.clear();
            return false;
        }
    }
    return true;
}

bool SaveGameBinaryIO::WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    // Both save files and host storage use the same temporary-name convention.
    // Serialize writers in this process, including different storage instances.
    static std::mutex writeMutex;
    const std::scoped_lock lock{ writeMutex };
    if (!PrepareOutputPath(path)) {
        return false;
    }

    const std::filesystem::path tempPath = TempPathFor(path);
#if defined(_WIN32)
    std::FILE* output = nullptr;
    if (_wfopen_s(&output, tempPath.c_str(), L"wb") != 0) return false;
#else
    std::FILE* output = std::fopen(tempPath.c_str(), "wb");
    if (output == nullptr) return false;
#endif
    bool written = bytes.empty() || std::fwrite(bytes.data(), 1U, bytes.size(), output) == bytes.size();
    written = std::fflush(output) == 0 && written;
#if defined(_WIN32)
    if (written) written = _commit(_fileno(output)) == 0;
#elif !defined(__EMSCRIPTEN__)
    if (written) written = ::fsync(::fileno(output)) == 0;
#endif
    written = std::fclose(output) == 0 && written;
    if (!written) {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
        return false;
    }

    if (!ReplaceFileAtomically(tempPath, path)) {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
        return false;
    }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    // The rename is already atomic; flushing its directory makes the new name
    // durable as well. If this fails, the complete new file may already be visible.
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{ "." };
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) return false;
    const bool durable = ::fsync(directory) == 0;
    const bool closed = ::close(directory) == 0;
    if (!durable || !closed) return false;
#endif
    return true;
}

} // namespace kb::save
