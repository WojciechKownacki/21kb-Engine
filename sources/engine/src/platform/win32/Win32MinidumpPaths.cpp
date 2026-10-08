#include "Win32MinidumpPaths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <cstring>

namespace kb::platform {
namespace {

constexpr std::uint32_t kCodeViewPdb70Signature = 0x53445352U; // "RSDS"
constexpr std::size_t kCodeViewPdb70NameOffset = 24U;          // signature, GUID, age

[[nodiscard]] bool InBounds(std::span<std::byte> dump, std::uint64_t offset, std::uint64_t size) noexcept {
    return offset <= dump.size() && size <= dump.size() - offset;
}

template <typename T>
[[nodiscard]] bool Read(std::span<std::byte> dump, std::uint64_t offset, T& value) noexcept {
    if (!InBounds(dump, offset, sizeof(T))) {
        return false;
    }
    std::memcpy(&value, dump.data() + offset, sizeof(T));
    return true;
}

template <typename T>
void Write(std::span<std::byte> dump, std::uint64_t offset, const T& value) noexcept {
    std::memcpy(dump.data() + offset, &value, sizeof(T));
}

// MINIDUMP_STRING at `rva`: a byte length, then that many bytes of UTF-16 and a terminator.
[[nodiscard]] bool StripStringPath(std::span<std::byte> dump, RVA rva) noexcept {
    ULONG32 length = 0U;
    if (!Read(dump, rva, length) || length % 2U != 0U || !InBounds(dump, std::uint64_t{ rva } + 4U, length)) {
        return false;
    }
    std::byte* const text = dump.data() + rva + 4U;
    const std::size_t units = length / 2U;
    std::size_t nameStart = 0U;
    for (std::size_t index = 0U; index < units; ++index) {
        wchar_t unit = L'\0';
        std::memcpy(&unit, text + index * 2U, 2U);
        if (unit == L'\\' || unit == L'/') {
            nameStart = index + 1U;
        }
    }
    if (nameStart == 0U) {
        return true;
    }
    const std::size_t nameUnits = units - nameStart;
    std::memmove(text, text + nameStart * 2U, nameUnits * 2U);
    std::memset(text + nameUnits * 2U, 0, (units - nameUnits) * 2U);
    Write(dump, rva, static_cast<ULONG32>(nameUnits * 2U));
    return true;
}

// The PDB path of a CodeView PDB 7.0 record, cut to its file name. Other record kinds carry no
// path this module knows how to read and are left alone.
[[nodiscard]] bool StripCodeViewPath(std::span<std::byte> dump, MINIDUMP_LOCATION_DESCRIPTOR record) noexcept {
    if (record.DataSize == 0U) {
        return true;
    }
    if (!InBounds(dump, record.Rva, record.DataSize)) {
        return false;
    }
    std::uint32_t signature = 0U;
    if (record.DataSize <= kCodeViewPdb70NameOffset || !Read(dump, record.Rva, signature) ||
        signature != kCodeViewPdb70Signature) {
        return true;
    }
    char* const name = reinterpret_cast<char*>(dump.data() + record.Rva + kCodeViewPdb70NameOffset);
    const std::size_t capacity = record.DataSize - kCodeViewPdb70NameOffset;
    std::size_t length = 0U;
    while (length < capacity && name[length] != '\0') {
        ++length;
    }
    std::size_t nameStart = 0U;
    for (std::size_t index = 0U; index < length; ++index) {
        if (name[index] == '\\' || name[index] == '/') {
            nameStart = index + 1U;
        }
    }
    if (nameStart == 0U) {
        return true;
    }
    std::memmove(name, name + nameStart, length - nameStart);
    std::memset(name + (length - nameStart), 0, capacity - (length - nameStart));
    return true;
}

[[nodiscard]] bool StripModuleList(std::span<std::byte> dump, const MINIDUMP_DIRECTORY& entry) noexcept {
    ULONG32 count = 0U;
    if (!Read(dump, entry.Location.Rva, count) ||
        static_cast<std::uint64_t>(count) * sizeof(MINIDUMP_MODULE) + sizeof(ULONG32) > entry.Location.DataSize) {
        return false;
    }
    for (ULONG32 index = 0U; index < count; ++index) {
        MINIDUMP_MODULE module{};
        if (!Read(dump, std::uint64_t{ entry.Location.Rva } + sizeof(ULONG32) + std::uint64_t{ index } * sizeof(MINIDUMP_MODULE), module) ||
            !StripStringPath(dump, module.ModuleNameRva) || !StripCodeViewPath(dump, module.CvRecord)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool StripUnloadedModuleList(std::span<std::byte> dump, const MINIDUMP_DIRECTORY& entry) noexcept {
    MINIDUMP_UNLOADED_MODULE_LIST list{};
    if (!Read(dump, entry.Location.Rva, list) || list.SizeOfHeader < sizeof(list) ||
        list.SizeOfEntry < sizeof(MINIDUMP_UNLOADED_MODULE) ||
        std::uint64_t{ list.SizeOfHeader } + std::uint64_t{ list.SizeOfEntry } * list.NumberOfEntries > entry.Location.DataSize) {
        return false;
    }
    for (ULONG32 index = 0U; index < list.NumberOfEntries; ++index) {
        MINIDUMP_UNLOADED_MODULE module{};
        if (!Read(dump, std::uint64_t{ entry.Location.Rva } + list.SizeOfHeader + std::uint64_t{ index } * list.SizeOfEntry, module) ||
            !StripStringPath(dump, module.ModuleNameRva)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool StripMinidumpModulePaths(std::span<std::byte> dump) noexcept {
    MINIDUMP_HEADER header{};
    if (!Read(dump, 0U, header) || header.Signature != MINIDUMP_SIGNATURE ||
        !InBounds(dump, header.StreamDirectoryRva, std::uint64_t{ header.NumberOfStreams } * sizeof(MINIDUMP_DIRECTORY))) {
        return false;
    }
    for (ULONG32 index = 0U; index < header.NumberOfStreams; ++index) {
        MINIDUMP_DIRECTORY entry{};
        if (!Read(dump, std::uint64_t{ header.StreamDirectoryRva } + std::uint64_t{ index } * sizeof(MINIDUMP_DIRECTORY), entry) ||
            !InBounds(dump, entry.Location.Rva, entry.Location.DataSize)) {
            return false;
        }
        if (entry.StreamType == ModuleListStream && !StripModuleList(dump, entry)) {
            return false;
        }
        if (entry.StreamType == UnloadedModuleListStream && !StripUnloadedModuleList(dump, entry)) {
            return false;
        }
    }
    return true;
}

} // namespace kb::platform
