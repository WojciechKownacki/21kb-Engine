#include "engine/platform/CrashReporting.hpp"

#include "Win32MinidumpPaths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <shlobj.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <span>
#include <thread>

// Nothing on the crash path may allocate, take a lock the crashed code could
// hold, or depend on more than a few hundred bytes of the crashed thread's stack:
// the heap may be the thing that broke and the stack may be exhausted. The
// crashing thread therefore only records where it stopped and wakes the
// reporter thread, which writes from buffers reserved here at load time.

namespace kb::platform {
namespace {

constexpr std::size_t kPathCapacity = 1024U;
constexpr std::size_t kTextCapacity = 256U;
constexpr std::size_t kReasonCapacity = 512U;
constexpr std::size_t kNoteCount = 64U;
constexpr std::size_t kNoteBytes = 240U;
constexpr std::size_t kMetadataCapacity = 96U * 1024U;
constexpr std::size_t kModuleCapacity = 1024U;
constexpr DWORD kReporterWaitMilliseconds = 120'000U;
constexpr int kNonExceptionExitCode = 3;

// Custom codes for the reports that do not start from a structured exception;
// they mark the exception record the dump carries.
constexpr DWORD kTerminateCode = 0xE0216B01U;
constexpr DWORD kAbortCode = 0xE0216B02U;
constexpr DWORD kPureCallCode = 0xE0216B03U;
constexpr DWORD kInvalidParameterCode = 0xE0216B04U;

using MiniDumpWriteDumpFunction = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
using GetFileVersionInfoSizeFunction = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
using GetFileVersionInfoFunction = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
using VerQueryValueFunction = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

struct NoteSlot {
    std::atomic<std::uint32_t> length{ 0U };
    char text[kNoteBytes]{};
};

struct ReporterState {
    std::atomic<bool> installed{ false };
    std::atomic<bool> ready{ false };
    std::atomic<LONG> handling{ 0 };
    std::atomic<DWORD> handlingThread{ 0U };

    wchar_t directory[kPathCapacity]{};
    wchar_t executablePath[kPathCapacity]{};
    wchar_t executableName[kTextCapacity]{};
    char product[kTextCapacity]{};
    char version[kTextCapacity]{};
    // Paths in the description replace this prefix with %USERPROFILE%, and any path
    // component that is the account name with %USERNAME%.
    char profileUtf8[kPathCapacity]{};
    char userNameUtf8[kTextCapacity]{};
    bool uploadPendingReports = false;
    CrashReportWrittenCallback callback = nullptr;

    HANDLE requestEvent = nullptr;
    HANDLE doneEvent = nullptr;
    DWORD reporterThreadId = 0U;
    MiniDumpWriteDumpFunction writeDump = nullptr;

    // The request the crashing thread hands over.
    EXCEPTION_POINTERS* pointers = nullptr;
    DWORD crashedThreadId = 0U;
    char reason[kReasonCapacity]{};

    std::atomic<std::uint32_t> nextNote{ 0U };
    NoteSlot notes[kNoteCount]{};

    char metadata[kMetadataCapacity]{};
    HMODULE modules[kModuleCapacity]{};
};

ReporterState g_state;

void CopyText(char* destination, std::size_t capacity, const char* source, std::size_t length) noexcept {
    if (capacity == 0U) {
        return;
    }
    const std::size_t count = length < capacity - 1U ? length : capacity - 1U;
    if (count != 0U) {
        std::memcpy(destination, source, count);
    }
    destination[count] = '\0';
}

void CopyWide(wchar_t* destination, std::size_t capacity, const wchar_t* source) noexcept {
    std::size_t index = 0U;
    for (; source != nullptr && source[index] != L'\0' && index + 1U < capacity; ++index) {
        destination[index] = source[index];
    }
    destination[index] = L'\0';
}

void Utf8FromWide(const wchar_t* source, char* destination, std::size_t capacity) noexcept {
    destination[0] = '\0';
    const int written = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination,
        static_cast<int>(capacity), nullptr, nullptr);
    if (written <= 0) {
        destination[0] = '\0';
    }
}

// Bounded writer over a fixed buffer. Overflow truncates; the caller checks
// Overflowed() only where a cut-off document would be wrong.
class FixedWriter final {
public:
    FixedWriter(char* buffer, std::size_t capacity) noexcept : buffer_(buffer), capacity_(capacity) {}

    void Append(const char* text, std::size_t length) noexcept {
        for (std::size_t index = 0U; index < length; ++index) {
            Put(text[index]);
        }
    }
    void Append(const char* text) noexcept { Append(text, std::strlen(text)); }
    void Put(char character) noexcept {
        if (size_ + 1U < capacity_) {
            buffer_[size_++] = character;
        } else {
            overflowed_ = true;
        }
    }
    void Decimal(std::uint64_t value) noexcept {
        char digits[24];
        std::size_t count = 0U;
        do {
            digits[count++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);
        while (count != 0U) {
            Put(digits[--count]);
        }
    }
    void Hex(std::uint64_t value, std::size_t minimumDigits = 1U) noexcept {
        constexpr char kDigits[] = "0123456789abcdef";
        char digits[16];
        std::size_t count = 0U;
        do {
            digits[count++] = kDigits[value & 0xFU];
            value >>= 4U;
        } while (value != 0U);
        while (count < minimumDigits && count < sizeof(digits)) {
            digits[count++] = '0';
        }
        while (count != 0U) {
            Put(digits[--count]);
        }
    }
    void Padded(std::uint32_t value, std::size_t width) noexcept {
        char digits[10];
        for (std::size_t index = width; index != 0U; --index) {
            digits[index - 1U] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        }
        Append(digits, width);
    }
    // JSON string body (no quotes). The user profile prefix becomes
    // %USERPROFILE%, and a path component that is the account name (a profile
    // on another drive, a shared folder per user) becomes %USERNAME%, so a
    // description never names the account it came from.
    void JsonText(const char* text, std::size_t length) noexcept {
        const std::size_t profileLength = std::strlen(g_state.profileUtf8);
        const std::size_t userNameLength = std::strlen(g_state.userNameUtf8);
        for (std::size_t index = 0U; index < length;) {
            if (profileLength >= 4U && MatchesProfile(text + index, length - index, profileLength)) {
                Append("%USERPROFILE%");
                index += profileLength;
                continue;
            }
            if (userNameLength != 0U && index != 0U && IsSeparator(text[index - 1U]) &&
                MatchesComponent(text + index, length - index, g_state.userNameUtf8, userNameLength)) {
                Append("%USERNAME%");
                index += userNameLength;
                continue;
            }
            const unsigned char character = static_cast<unsigned char>(text[index++]);
            switch (character) {
            case '"': Append("\\\""); break;
            case '\\': Append("\\\\"); break;
            case '\n': Append("\\n"); break;
            case '\r': Append("\\r"); break;
            case '\t': Append("\\t"); break;
            default:
                if (character < 0x20U) {
                    Append("\\u00");
                    Hex(character, 2U);
                } else {
                    Put(static_cast<char>(character));
                }
                break;
            }
        }
    }
    void JsonString(const char* text) noexcept {
        Put('"');
        JsonText(text, std::strlen(text));
        Put('"');
    }
    void JsonWide(const wchar_t* text) noexcept {
        char utf8[kPathCapacity];
        Utf8FromWide(text, utf8, sizeof(utf8));
        JsonString(utf8);
    }

    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] bool Overflowed() const noexcept { return overflowed_; }
    void Terminate() noexcept { buffer_[size_ < capacity_ ? size_ : capacity_ - 1U] = '\0'; }

private:
    [[nodiscard]] static bool IsSeparator(char character) noexcept {
        return character == '\\' || character == '/';
    }
    [[nodiscard]] static char FoldAscii(char character) noexcept {
        return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
    }
    // `name` as a whole path component at the start of `text`: a separator, a quote,
    // a space or the end follows it.
    [[nodiscard]] static bool MatchesComponent(const char* text, std::size_t available, const char* name,
        std::size_t nameLength) noexcept {
        if (available < nameLength) {
            return false;
        }
        for (std::size_t index = 0U; index < nameLength; ++index) {
            if (FoldAscii(text[index]) != FoldAscii(name[index])) {
                return false;
            }
        }
        const char next = available > nameLength ? text[nameLength] : '\0';
        return next == '\0' || IsSeparator(next) || next == '"' || next == ' ';
    }
    [[nodiscard]] static bool MatchesProfile(const char* text, std::size_t available, std::size_t profileLength) noexcept {
        if (available < profileLength) {
            return false;
        }
        for (std::size_t index = 0U; index < profileLength; ++index) {
            char left = text[index];
            char right = g_state.profileUtf8[index];
            if (left == '/') left = '\\';
            if (right == '/') right = '\\';
            if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
            if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
            if (left != right) {
                return false;
            }
        }
        // Whole path components only: C:\Users\ann must not rewrite C:\Users\anna.
        const char next = available > profileLength ? text[profileLength] : '\0';
        return next == '\0' || next == '\\' || next == '/' || next == '"' || next == ' ';
    }

    char* buffer_;
    std::size_t capacity_;
    std::size_t size_ = 0U;
    bool overflowed_ = false;
};

[[nodiscard]] const wchar_t* FileNamePart(const wchar_t* path) noexcept {
    const wchar_t* name = path;
    for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            name = cursor + 1;
        }
    }
    return name;
}

[[nodiscard]] const char* FileNamePart(const char* path) noexcept {
    const char* name = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            name = cursor + 1;
        }
    }
    return name;
}

struct CodeViewRecord {
    DWORD signature;
    GUID guid;
    DWORD age;
    char pdbName[1];
};

struct ModuleIdentity {
    // The image's own identity: with its file name, what a symbol server files the
    // executable under.
    bool imageFound = false;
    DWORD timeDateStamp = 0U;
    DWORD sizeOfImage = 0U;
    // The CodeView record: what it files the PDB under.
    bool found = false;
    GUID guid{};
    DWORD age = 0U;
    char pdbName[kTextCapacity]{};
};

// Reads the CodeView record from a module already mapped in this process: the
// GUID and age a symbol server files the matching PDB under. Guarded, since a
// module can be unmapped or malformed at the moment of a crash.
ModuleIdentity ReadModuleIdentity(HMODULE module) noexcept {
    ModuleIdentity identity{};
    __try {
        const auto* base = reinterpret_cast<const std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return identity;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return identity;
        }
        identity.imageFound = true;
        identity.timeDateStamp = nt->FileHeader.TimeDateStamp;
        identity.sizeOfImage = nt->OptionalHeader.SizeOfImage;
        if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
            return identity;
        }
        const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
        const std::size_t count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (std::size_t index = 0U; index < count && directory.VirtualAddress != 0U; ++index) {
            if (entries[index].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[index].AddressOfRawData == 0U ||
                entries[index].SizeOfData < sizeof(CodeViewRecord)) {
                continue;
            }
            const auto* record = reinterpret_cast<const CodeViewRecord*>(base + entries[index].AddressOfRawData);
            if (record->signature != 0x53445352U) { // "RSDS"
                continue;
            }
            identity.found = true;
            identity.guid = record->guid;
            identity.age = record->age;
            const std::size_t nameBytes = entries[index].SizeOfData - offsetof(CodeViewRecord, pdbName);
            const std::size_t length = strnlen(record->pdbName, nameBytes);
            // The PDB path is the build machine's; only its file name is kept.
            char full[kPathCapacity];
            CopyText(full, sizeof(full), record->pdbName, length);
            const char* name = FileNamePart(full);
            CopyText(identity.pdbName, sizeof(identity.pdbName), name, std::strlen(name));
            return identity;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        identity.imageFound = false;
        identity.found = false;
    }
    return identity;
}

void WriteImageId(FixedWriter& writer, const ModuleIdentity& identity) noexcept {
    // Symbol store layout for an executable: the time stamp as 8 upper-case hex
    // digits, then the image size in hex.
    char text[32];
    FixedWriter id{ text, sizeof(text) };
    id.Hex(identity.timeDateStamp, 8U);
    id.Hex(identity.sizeOfImage);
    id.Terminate();
    for (char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor >= 'a' && *cursor <= 'f') {
            *cursor = static_cast<char>(*cursor - 'a' + 'A');
        }
    }
    writer.JsonString(text);
}

void WriteIdentity(FixedWriter& writer, const ModuleIdentity& identity) noexcept {
    // Symbol store layout: GUID as 32 upper-case hex digits, then the age in hex.
    char text[48];
    FixedWriter id{ text, sizeof(text) };
    id.Hex(identity.guid.Data1, 8U);
    id.Hex(identity.guid.Data2, 4U);
    id.Hex(identity.guid.Data3, 4U);
    for (const unsigned char byte : identity.guid.Data4) {
        id.Hex(byte, 2U);
    }
    id.Hex(identity.age);
    id.Terminate();
    for (char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor >= 'a' && *cursor <= 'f') {
            *cursor = static_cast<char>(*cursor - 'a' + 'A');
        }
    }
    writer.JsonString(text);
}

[[nodiscard]] bool EnsureDirectory(const wchar_t* path) noexcept {
    wchar_t partial[kPathCapacity];
    std::size_t length = 0U;
    for (; path[length] != L'\0' && length + 1U < kPathCapacity; ++length) {
        partial[length] = path[length];
        if ((path[length] == L'\\' || path[length] == L'/') && length > 2U) {
            partial[length] = L'\0';
            CreateDirectoryW(partial, nullptr);
            partial[length] = path[length];
        }
    }
    partial[length] = L'\0';
    if (CreateDirectoryW(partial, nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

[[nodiscard]] bool AppendPath(wchar_t* destination, std::size_t capacity, const wchar_t* directory,
    const wchar_t* name, const wchar_t* extension) noexcept {
    std::size_t length = 0U;
    const auto append = [&](const wchar_t* text) {
        for (; *text != L'\0'; ++text) {
            if (length + 1U >= capacity) {
                return false;
            }
            destination[length++] = *text;
        }
        return true;
    };
    const bool ok = append(directory) && append(L"\\") && append(name) && append(extension);
    destination[ok ? length : 0U] = L'\0';
    return ok;
}

[[nodiscard]] bool WriteWholeFile(const wchar_t* path, const char* data, std::size_t size) noexcept {
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0U;
    const bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) != FALSE &&
        written == size;
    CloseHandle(file);
    return ok;
}

[[nodiscard]] const char* ExceptionName(DWORD code) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access-violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack-overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal-instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer-divide-by-zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page-error";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged-instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array-bounds-exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "datatype-misalignment";
    case 0xE06D7363U: return "unhandled-c++-exception";
    case kTerminateCode: return "std-terminate";
    case kAbortCode: return "abort";
    case kPureCallCode: return "pure-virtual-call";
    case kInvalidParameterCode: return "invalid-parameter";
    default: return "unhandled-exception";
    }
}

// Composes the JSON description for the request in g_state. Module names only,
// never paths, and the recent log with the profile prefix replaced.
std::size_t ComposeMetadata(const SYSTEMTIME& time, const wchar_t* dumpName) noexcept {
    FixedWriter writer{ g_state.metadata, sizeof(g_state.metadata) };
    const EXCEPTION_RECORD* record = g_state.pointers != nullptr ? g_state.pointers->ExceptionRecord : nullptr;
    const DWORD code = record != nullptr ? record->ExceptionCode : 0U;
    const auto address = reinterpret_cast<std::uintptr_t>(record != nullptr ? record->ExceptionAddress : nullptr);

    writer.Append("{\"schema\":\"21kb.crash-report/2\",\"product\":");
    writer.JsonString(g_state.product);
    writer.Append(",\"version\":");
    writer.JsonString(g_state.version);
    const ModuleIdentity executable = ReadModuleIdentity(GetModuleHandleW(nullptr));
    writer.Append(",\"buildId\":");
    if (executable.found) {
        WriteIdentity(writer, executable);
    } else {
        writer.Append("null");
    }
    writer.Append(",\"executable\":");
    writer.JsonWide(g_state.executableName);
    writer.Append(",\"timestampUtc\":\"");
    writer.Padded(time.wYear, 4U);
    writer.Put('-');
    writer.Padded(time.wMonth, 2U);
    writer.Put('-');
    writer.Padded(time.wDay, 2U);
    writer.Put('T');
    writer.Padded(time.wHour, 2U);
    writer.Put(':');
    writer.Padded(time.wMinute, 2U);
    writer.Put(':');
    writer.Padded(time.wSecond, 2U);
    writer.Append("Z\",\"reason\":");
    writer.JsonString(g_state.reason);
    writer.Append(",\"exception\":{\"code\":\"0x");
    writer.Hex(code, 8U);
    writer.Append("\",\"name\":\"");
    writer.Append(ExceptionName(code));
    writer.Append("\",\"module\":");

    std::uintptr_t faultBase = 0U;
    wchar_t faultModule[kPathCapacity]{};
    HMODULE owner = nullptr;
    if (address != 0U && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &owner) != FALSE &&
        GetModuleFileNameW(owner, faultModule, static_cast<DWORD>(kPathCapacity)) != 0U) {
        faultBase = reinterpret_cast<std::uintptr_t>(owner);
        writer.JsonWide(FileNamePart(faultModule));
        writer.Append(",\"offset\":\"0x");
        writer.Hex(address - faultBase);
        writer.Put('"');
    } else {
        writer.Append("null");
    }
    writer.Append("},\"processId\":");
    writer.Decimal(GetCurrentProcessId());
    writer.Append(",\"threadId\":");
    writer.Decimal(g_state.crashedThreadId);
    writer.Append(",\"minidump\":");
    writer.JsonWide(dumpName);

    writer.Append(",\"modules\":[");
    DWORD neededBytes = 0U;
    std::size_t moduleCount = 0U;
    if (EnumProcessModules(GetCurrentProcess(), g_state.modules, static_cast<DWORD>(sizeof(g_state.modules)),
            &neededBytes) != FALSE) {
        moduleCount = neededBytes / sizeof(HMODULE);
        if (moduleCount > kModuleCapacity) {
            moduleCount = kModuleCapacity;
        }
    }
    bool firstModule = true;
    for (std::size_t index = 0U; index < moduleCount; ++index) {
        wchar_t path[kPathCapacity];
        MODULEINFO info{};
        if (GetModuleFileNameW(g_state.modules[index], path, static_cast<DWORD>(kPathCapacity)) == 0U ||
            GetModuleInformation(GetCurrentProcess(), g_state.modules[index], &info, sizeof(info)) == FALSE) {
            continue;
        }
        if (!firstModule) {
            writer.Put(',');
        }
        firstModule = false;
        writer.Append("{\"name\":");
        writer.JsonWide(FileNamePart(path));
        writer.Append(",\"base\":\"0x");
        writer.Hex(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll));
        writer.Append("\",\"size\":");
        writer.Decimal(info.SizeOfImage);
        const ModuleIdentity identity = ReadModuleIdentity(g_state.modules[index]);
        if (identity.imageFound) {
            writer.Append(",\"imageId\":");
            WriteImageId(writer, identity);
        }
        if (identity.found) {
            writer.Append(",\"pdb\":");
            writer.JsonString(identity.pdbName);
            writer.Append(",\"pdbId\":");
            WriteIdentity(writer, identity);
        }
        writer.Put('}');
    }

    writer.Append("],\"log\":[");
    const std::uint32_t next = g_state.nextNote.load(std::memory_order_acquire);
    const std::uint32_t first = next > kNoteCount ? next - static_cast<std::uint32_t>(kNoteCount) : 0U;
    bool firstLine = true;
    for (std::uint32_t sequence = first; sequence < next; ++sequence) {
        const NoteSlot& slot = g_state.notes[sequence % kNoteCount];
        const std::uint32_t length = slot.length.load(std::memory_order_acquire);
        if (length == 0U || length > kNoteBytes) {
            continue;
        }
        if (!firstLine) {
            writer.Put(',');
        }
        firstLine = false;
        writer.Put('"');
        writer.JsonText(slot.text, length);
        writer.Put('"');
    }
    writer.Append("]}\n");
    if (writer.Overflowed()) {
        // A description cut off mid-document is not JSON; keep the identity and drop the rest.
        FixedWriter fallback{ g_state.metadata, sizeof(g_state.metadata) };
        fallback.Append("{\"schema\":\"21kb.crash-report/2\",\"product\":");
        fallback.JsonString(g_state.product);
        fallback.Append(",\"version\":");
        fallback.JsonString(g_state.version);
        fallback.Append(",\"reason\":");
        fallback.JsonString(g_state.reason);
        fallback.Append(",\"minidump\":");
        fallback.JsonWide(dumpName);
        fallback.Append(",\"truncated\":true}\n");
        fallback.Terminate();
        return fallback.Size();
    }
    writer.Terminate();
    return writer.Size();
}

// Strips the module paths of the dump just written, through a mapping of the
// file: nothing here allocates from the heap.
[[nodiscard]] bool StripModulePathsInFile(HANDLE file) noexcept {
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart <= 0) {
        return false;
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0U, 0U, nullptr);
    if (mapping == nullptr) {
        return false;
    }
    bool stripped = false;
    if (void* const view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0U, 0U, 0U); view != nullptr) {
        stripped = StripMinidumpModulePaths(
            std::span<std::byte>{ static_cast<std::byte*>(view), static_cast<std::size_t>(size.QuadPart) });
        stripped = FlushViewOfFile(view, 0U) != FALSE && stripped;
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    return stripped;
}

// Writes the dump and its description for the request in g_state. Runs on the
// reporter thread, or on the crashing thread when no reporter thread exists.
void WriteReport() noexcept {
    if (!EnsureDirectory(g_state.directory)) {
        return;
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t stem[96];
    {
        char narrow[96];
        FixedWriter name{ narrow, sizeof(narrow) };
        name.Append("crash-");
        name.Padded(time.wYear, 4U);
        name.Padded(time.wMonth, 2U);
        name.Padded(time.wDay, 2U);
        name.Put('-');
        name.Padded(time.wHour, 2U);
        name.Padded(time.wMinute, 2U);
        name.Padded(time.wSecond, 2U);
        name.Put('-');
        name.Decimal(GetCurrentProcessId());
        name.Terminate();
        std::size_t index = 0U;
        for (; narrow[index] != '\0'; ++index) {
            stem[index] = static_cast<wchar_t>(narrow[index]);
        }
        stem[index] = L'\0';
    }
    wchar_t dumpPath[kPathCapacity];
    wchar_t metadataPath[kPathCapacity];
    if (!AppendPath(dumpPath, kPathCapacity, g_state.directory, stem, L".dmp") ||
        !AppendPath(metadataPath, kPathCapacity, g_state.directory, stem, L".json")) {
        return;
    }
    wchar_t dumpName[96];
    CopyWide(dumpName, 96U, FileNamePart(dumpPath));
    const std::size_t metadataSize = ComposeMetadata(time, dumpName);

    bool dumped = false;
    if (g_state.writeDump != nullptr) {
        const HANDLE file = CreateFileW(dumpPath, GENERIC_READ | GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{};
            exception.ThreadId = g_state.crashedThreadId;
            exception.ExceptionPointers = g_state.pointers;
            exception.ClientPointers = FALSE;
            MINIDUMP_USER_STREAM comment{};
            comment.Type = CommentStreamA;
            comment.BufferSize = static_cast<ULONG>(metadataSize);
            comment.Buffer = g_state.metadata;
            MINIDUMP_USER_STREAM_INFORMATION streams{};
            streams.UserStreamCount = 1U;
            streams.UserStreamArray = &comment;
            // Every thread's stack, the module list and thread state; no heap and
            // no process environment block, which would carry the environment.
            const auto type = static_cast<MINIDUMP_TYPE>(
                MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
            dumped = g_state.writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                g_state.pointers != nullptr ? &exception : nullptr, &streams, nullptr) != FALSE;
            // The module lists name every folder a module was loaded from; a report
            // keeps only file names, or it is not kept.
            dumped = dumped && StripModulePathsInFile(file);
            CloseHandle(file);
            if (!dumped) {
                DeleteFileW(dumpPath);
            }
        }
    }
    // The description is written last: a dump without one is incomplete and
    // is neither listed nor uploaded.
    if (dumped) {
        static_cast<void>(WriteWholeFile(metadataPath, g_state.metadata, metadataSize));
    }
    if (g_state.callback != nullptr) {
        g_state.callback(g_state.reason);
    }
}

void LoadSystemLibraries() noexcept {
    if (const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        dbghelp != nullptr) {
        g_state.writeDump = reinterpret_cast<MiniDumpWriteDumpFunction>(
            reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    }
}

void CopyVersionString(const void* block, VerQueryValueFunction query, const wchar_t* key,
    char* destination, std::size_t capacity) noexcept {
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT translationBytes = 0U;
    if (destination[0] != '\0' ||
        query(block, L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes) == FALSE ||
        translationBytes < sizeof(Translation)) {
        return;
    }
    wchar_t subBlock[96];
    constexpr wchar_t kDigits[] = L"0123456789abcdef";
    const wchar_t prefix[] = L"\\StringFileInfo\\";
    std::size_t length = 0U;
    for (const wchar_t character : prefix) {
        if (character != L'\0') subBlock[length++] = character;
    }
    const unsigned value = (static_cast<unsigned>(translations[0].language) << 16U) | translations[0].codePage;
    for (int shift = 28; shift >= 0; shift -= 4) {
        subBlock[length++] = kDigits[(value >> static_cast<unsigned>(shift)) & 0xFU];
    }
    subBlock[length++] = L'\\';
    for (; *key != L'\0' && length + 1U < 96U; ++key) {
        subBlock[length++] = *key;
    }
    subBlock[length] = L'\0';
    wchar_t* text = nullptr;
    UINT characters = 0U;
    if (query(block, subBlock, reinterpret_cast<void**>(&text), &characters) != FALSE && text != nullptr && characters > 1U) {
        Utf8FromWide(text, destination, capacity);
    }
}

// Fills product and version from the executable's version resource when the
// host did not name them. Runs on the reporter thread, off the startup path.
void ReadVersionResource() noexcept {
    if (g_state.product[0] != '\0' && g_state.version[0] != '\0') {
        return;
    }
    const HMODULE library = LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library != nullptr) {
        const auto size = reinterpret_cast<GetFileVersionInfoSizeFunction>(
            reinterpret_cast<void*>(GetProcAddress(library, "GetFileVersionInfoSizeW")));
        const auto read = reinterpret_cast<GetFileVersionInfoFunction>(
            reinterpret_cast<void*>(GetProcAddress(library, "GetFileVersionInfoW")));
        const auto query = reinterpret_cast<VerQueryValueFunction>(
            reinterpret_cast<void*>(GetProcAddress(library, "VerQueryValueW")));
        DWORD ignored = 0U;
        const DWORD bytes = size != nullptr ? size(g_state.executablePath, &ignored) : 0U;
        if (read != nullptr && query != nullptr && bytes != 0U && bytes <= 64U * 1024U) {
            alignas(8) static char block[64U * 1024U];
            if (read(g_state.executablePath, 0U, bytes, block) != FALSE) {
                CopyVersionString(block, query, L"ProductName", g_state.product, sizeof(g_state.product));
                CopyVersionString(block, query, L"ProductVersion", g_state.version, sizeof(g_state.version));
            }
        }
    }
    if (g_state.product[0] == '\0') {
        Utf8FromWide(g_state.executableName, g_state.product, sizeof(g_state.product));
    }
    if (g_state.version[0] == '\0') {
        CopyText(g_state.version, sizeof(g_state.version), "unversioned", 11U);
    }
}

void ReadProfilePrefix() noexcept {
    wchar_t profile[kPathCapacity];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(kPathCapacity));
    if (length != 0U && length < kPathCapacity) {
        Utf8FromWide(profile, g_state.profileUtf8, sizeof(g_state.profileUtf8));
    }
    wchar_t userName[kTextCapacity];
    const DWORD nameLength = GetEnvironmentVariableW(L"USERNAME", userName, static_cast<DWORD>(kTextCapacity));
    // One- and two-letter names would rewrite ordinary folder names.
    if (nameLength >= 3U && nameLength < kTextCapacity) {
        Utf8FromWide(userName, g_state.userNameUtf8, sizeof(g_state.userNameUtf8));
    }
}

void StartPendingUpload() noexcept {
    wchar_t executableDirectory[kPathCapacity];
    CopyWide(executableDirectory, kPathCapacity, g_state.executablePath);
    wchar_t* name = const_cast<wchar_t*>(FileNamePart(executableDirectory));
    *name = L'\0';
    try {
        const std::filesystem::path config =
            std::filesystem::path{ executableDirectory } / kCrashUploadConfigFile;
        std::error_code error;
        if (!std::filesystem::is_regular_file(config, error)) {
            return;
        }
        std::string endpoint = ReadCrashUploadEndpoint(config);
        const std::filesystem::path directory{ g_state.directory };
        if (endpoint.empty() || !HasCrashUploadConsent(directory) || ListCrashReports(directory).empty()) {
            return;
        }
        // A separate, short-lived thread: the reporter thread must stay free to
        // write a dump should this run crash while an old report is being sent.
        std::thread{ [directory, endpoint = std::move(endpoint)] {
            static_cast<void>(UploadPendingCrashReports(directory, endpoint));
        } }.detach();
    } catch (...) {
    }
}

DWORD WINAPI ReporterThread(void*) {
    LoadSystemLibraries();
    ReadVersionResource();
    ReadProfilePrefix();
    g_state.ready.store(true, std::memory_order_release);
    if (g_state.uploadPendingReports) {
        StartPendingUpload();
    }
    for (;;) {
        if (WaitForSingleObject(g_state.requestEvent, INFINITE) != WAIT_OBJECT_0) {
            return 1U;
        }
        WriteReport();
        SetEvent(g_state.doneEvent);
    }
}

[[noreturn]] void TerminateAfterReport(UINT exitCode) noexcept {
    TerminateProcess(GetCurrentProcess(), exitCode);
    for (;;) {
        Sleep(INFINITE);
    }
}

// The single entry for every kind of crash. Returns only to the first crashing
// thread, after the report is written; any other thread parks here until the
// process ends.
void ReportCrash(EXCEPTION_POINTERS* pointers, const char* reason) noexcept {
    const DWORD self = GetCurrentThreadId();
    if (self == g_state.reporterThreadId || g_state.handlingThread.load() == self) {
        // The reporter itself failed, or the handler faulted: nothing safe is left.
        TerminateAfterReport(static_cast<UINT>(kNonExceptionExitCode));
    }
    LONG expected = 0;
    if (!g_state.handling.compare_exchange_strong(expected, 1)) {
        for (;;) {
            Sleep(INFINITE);
        }
    }
    g_state.handlingThread.store(self);
    g_state.pointers = pointers;
    g_state.crashedThreadId = self;
    CopyText(g_state.reason, sizeof(g_state.reason), reason, std::strlen(reason));
    if (g_state.requestEvent != nullptr && g_state.ready.load(std::memory_order_acquire)) {
        SetEvent(g_state.requestEvent);
        WaitForSingleObject(g_state.doneEvent, kReporterWaitMilliseconds);
    } else {
        if (g_state.writeDump == nullptr) {
            LoadSystemLibraries();
        }
        WriteReport();
    }
}

// Reports from a handler that has no exception record: the record names the
// handler's own context, so the dump's faulting stack is the call that ended here.
[[noreturn]] __declspec(noinline) void ReportWithoutException(DWORD code, const char* reason) noexcept {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = code;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_ARM64)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Pc);
#endif
    EXCEPTION_POINTERS pointers{ &record, &context };
    ReportCrash(&pointers, reason);
    TerminateAfterReport(static_cast<UINT>(kNonExceptionExitCode));
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* pointers) {
    ReportCrash(pointers, "unhandled-exception");
    const DWORD code = pointers != nullptr && pointers->ExceptionRecord != nullptr
        ? pointers->ExceptionRecord->ExceptionCode : static_cast<DWORD>(kNonExceptionExitCode);
    TerminateAfterReport(code);
}

[[noreturn]] void TerminateHandler() noexcept {
    char reason[kReasonCapacity] = "std-terminate";
    try {
        if (const std::exception_ptr current = std::current_exception(); current != nullptr) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& error) {
                FixedWriter writer{ reason, sizeof(reason) };
                writer.Append("std-terminate: ");
                writer.Append(error.what());
                writer.Terminate();
            } catch (...) {
                constexpr char kUnknown[] = "std-terminate: unknown exception";
                CopyText(reason, sizeof(reason), kUnknown, sizeof(kUnknown) - 1U);
            }
        }
    } catch (...) {
    }
    ReportWithoutException(kTerminateCode, reason);
}

void __cdecl AbortSignalHandler(int) {
    ReportWithoutException(kAbortCode, "abort");
}

void __cdecl PureCallHandler() {
    ReportWithoutException(kPureCallCode, "pure-virtual-call");
}

void __cdecl InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t) {
    ReportWithoutException(kInvalidParameterCode, "invalid-parameter");
}

[[nodiscard]] std::wstring ExecutableStem(const wchar_t* path) {
    std::wstring name{ FileNamePart(path) };
    if (const std::size_t dot = name.rfind(L'.'); dot != std::wstring::npos && dot != 0U) {
        name.resize(dot);
    }
    return name;
}

} // namespace

std::filesystem::path DefaultCrashReportDirectory(std::wstring_view executableStem) {
    std::filesystem::path root;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0U, nullptr, &local)) && local != nullptr) {
        root = local;
    }
    CoTaskMemFree(local);
    if (root.empty()) {
        wchar_t value[kPathCapacity];
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, static_cast<DWORD>(kPathCapacity));
        if (length == 0U || length >= kPathCapacity) {
            return {};
        }
        root = value;
    }
    std::wstring stem{ executableStem };
    for (wchar_t& character : stem) {
        if (character < 0x20 || std::wstring_view{ L"<>:\"/\\|?*" }.find(character) != std::wstring_view::npos) {
            character = L'_';
        }
    }
    if (stem.empty()) {
        stem = L"Game";
    }
    return root / L"21kb" / L"CrashReports" / stem;
}

bool CrashReporter::Install(const CrashReporterOptions& options) {
    bool expected = false;
    if (!g_state.installed.compare_exchange_strong(expected, true)) {
        return false;
    }
    const DWORD length = GetModuleFileNameW(nullptr, g_state.executablePath, static_cast<DWORD>(kPathCapacity));
    if (length == 0U || length >= kPathCapacity) {
        g_state.executablePath[0] = L'\0';
    }
    CopyWide(g_state.executableName, kTextCapacity, FileNamePart(g_state.executablePath));
    std::filesystem::path directory = options.reportDirectory;
    if (directory.empty()) {
        directory = DefaultCrashReportDirectory(ExecutableStem(g_state.executablePath));
    }
    directory = directory.lexically_normal();
    if (directory.native().size() + 64U >= kPathCapacity) {
        directory.clear();
    }
    CopyWide(g_state.directory, kPathCapacity, directory.c_str());
    CopyText(g_state.product, sizeof(g_state.product), options.productName.data(), options.productName.size());
    CopyText(g_state.version, sizeof(g_state.version), options.version.data(), options.version.size());
    g_state.callback = options.onReportWritten;
    g_state.uploadPendingReports = options.uploadPendingReports;

    g_state.requestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_state.doneEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_state.requestEvent != nullptr && g_state.doneEvent != nullptr) {
        // The stack reserve covers dbghelp's stack walk; the thread does nothing
        // but wait until a crash.
        DWORD threadId = 0U;
        const HANDLE thread = CreateThread(nullptr, 256U * 1024U, &ReporterThread, nullptr,
            STACK_SIZE_PARAM_IS_A_RESERVATION, &threadId);
        if (thread != nullptr) {
            g_state.reporterThreadId = threadId;
            SetThreadDescription(thread, L"Crash reporter");
            CloseHandle(thread);
        }
    }

    SetUnhandledExceptionFilter(&UnhandledExceptionHandler);
    std::set_terminate(&TerminateHandler);
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    static_cast<void>(std::signal(SIGABRT, &AbortSignalHandler));
    static_cast<void>(_set_purecall_handler(&PureCallHandler));
    static_cast<void>(_set_invalid_parameter_handler(&InvalidParameterHandler));
    return true;
}

bool CrashReporter::IsInstalled() noexcept {
    return g_state.installed.load();
}

std::filesystem::path CrashReporter::ReportDirectory() {
    return IsInstalled() ? std::filesystem::path{ g_state.directory } : std::filesystem::path{};
}

void CrashReporter::Note(std::string_view line) noexcept {
    const std::uint32_t sequence = g_state.nextNote.fetch_add(1U, std::memory_order_acq_rel);
    NoteSlot& slot = g_state.notes[sequence % kNoteCount];
    const std::size_t length = line.size() < kNoteBytes ? line.size() : kNoteBytes;
    slot.length.store(0U, std::memory_order_release);
    if (length != 0U) {
        std::memcpy(slot.text, line.data(), length);
    }
    slot.length.store(static_cast<std::uint32_t>(length), std::memory_order_release);
}

} // namespace kb::platform
