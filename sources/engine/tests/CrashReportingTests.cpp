// Crash capture end to end: a helper process installs the reporter and fails in
// each supported way; the report it leaves is checked as a minidump and as JSON.
// Upload is checked against a loopback HTTP server owned by this test.

#include "engine/core/JsonValue.hpp"
#include "engine/platform/CrashReporting.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include "TestSupport.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using kb::tests::Require;

const std::filesystem::path kRoot{ KB_CRASH_REPORTING_TEST_ROOT };

[[nodiscard]] std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>{ stream }), std::istreambuf_iterator<char>{});
}

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

[[nodiscard]] std::filesystem::path FreshDirectory(std::string_view name) {
    const std::filesystem::path directory = kRoot / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory);
    return directory;
}

struct HelperRun {
    DWORD exitCode = 0U;
    std::string output;
};

// Runs the helper at `helper` (the built one by default) with `environment` (a
// double-NUL-terminated block; empty inherits this process's environment).
[[nodiscard]] HelperRun RunHelper(const std::filesystem::path& directory, std::wstring_view mode,
    const std::filesystem::path& helper = std::filesystem::path{ KB_CRASH_REPORTING_TEST_HELPER },
    std::wstring environment = {}) {
    SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    Require(CreatePipe(&readPipe, &writePipe, &inheritable, 0U) != FALSE &&
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0U) != FALSE, "helper output pipe could not be created");
    std::wstring command = L"\"" + helper.wstring() + L"\" \"" + directory.wstring() + L"\" " + std::wstring{ mode };
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.empty() ? nullptr : environment.data(), nullptr,
        &startup, &process);
    CloseHandle(writePipe);
    Require(created != FALSE, "crash helper could not be started");
    HelperRun run;
    char buffer[4096];
    DWORD read = 0U;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) != FALSE && read != 0U) {
        run.output.append(buffer, read);
    }
    CloseHandle(readPipe);
    const DWORD wait = WaitForSingleObject(process.hProcess, 120'000U);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1U);
    }
    Require(wait == WAIT_OBJECT_0, "crash helper did not end");
    GetExitCodeProcess(process.hProcess, &run.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return run;
}

struct DumpModule {
    std::wstring name;
    std::uint32_t timeDateStamp = 0U;
    std::uint32_t sizeOfImage = 0U;
    bool hasPdb = false;
    GUID pdbGuid{};
    std::uint32_t pdbAge = 0U;
    std::string pdbName;
};

struct DumpSummary {
    std::vector<DumpModule> modules;
    std::vector<std::wstring> unloadedModules;
    // Every byte range of process memory the dump carries: thread stacks and memory lists.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> memoryRanges;
    std::set<std::uint32_t> streams;
    std::uint32_t threadCount = 0U;
    std::uint32_t exceptionThread = 0U;
    std::uint32_t exceptionCode = 0U;
    bool crashedThreadHasStack = false;
    std::string comment;
};

[[nodiscard]] std::wstring ReadDumpString(const std::string& bytes, RVA rva) {
    ULONG32 length = 0U;
    Require(static_cast<std::uint64_t>(rva) + sizeof(length) <= bytes.size(), "minidump string runs past the file");
    std::memcpy(&length, bytes.data() + rva, sizeof(length));
    Require(length % 2U == 0U && static_cast<std::uint64_t>(rva) + sizeof(length) + length <= bytes.size(),
        "minidump string is malformed");
    std::wstring text(length / 2U, L'\0');
    std::memcpy(text.data(), bytes.data() + rva + sizeof(length), length);
    return text;
}

[[nodiscard]] DumpSummary ReadMinidumpBytes(const std::string& bytes);

// Validates the container by hand rather than through dbghelp, so the test
// checks the bytes a symbol server or crash backend would receive.
[[nodiscard]] DumpSummary ReadMinidump(const std::filesystem::path& path) {
    return ReadMinidumpBytes(ReadBytes(path));
}

[[nodiscard]] DumpSummary ReadMinidumpBytes(const std::string& bytes) {
    Require(bytes.size() >= sizeof(MINIDUMP_HEADER), "minidump is shorter than its header");
    MINIDUMP_HEADER header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    Require(header.Signature == MINIDUMP_SIGNATURE, "minidump does not start with MDMP");
    Require((header.Version & 0xFFFFU) == MINIDUMP_VERSION, "minidump version is not the documented one");
    Require(header.NumberOfStreams > 0U && header.NumberOfStreams < 256U, "minidump stream count is implausible");
    const std::uint64_t directoryEnd = static_cast<std::uint64_t>(header.StreamDirectoryRva) +
        static_cast<std::uint64_t>(header.NumberOfStreams) * sizeof(MINIDUMP_DIRECTORY);
    Require(directoryEnd <= bytes.size(), "minidump stream directory runs past the file");
    DumpSummary summary;
    for (std::uint32_t index = 0U; index < header.NumberOfStreams; ++index) {
        MINIDUMP_DIRECTORY entry{};
        std::memcpy(&entry, bytes.data() + header.StreamDirectoryRva + index * sizeof(MINIDUMP_DIRECTORY), sizeof(entry));
        if (entry.StreamType == UnusedStream) {
            continue;
        }
        Require(static_cast<std::uint64_t>(entry.Location.Rva) + entry.Location.DataSize <= bytes.size(),
            "minidump stream runs past the file");
        summary.streams.insert(entry.StreamType);
        const char* data = bytes.data() + entry.Location.Rva;
        if (entry.StreamType == ExceptionStream) {
            Require(entry.Location.DataSize >= sizeof(MINIDUMP_EXCEPTION_STREAM), "exception stream is truncated");
            MINIDUMP_EXCEPTION_STREAM exception{};
            std::memcpy(&exception, data, sizeof(exception));
            summary.exceptionThread = exception.ThreadId;
            summary.exceptionCode = exception.ExceptionRecord.ExceptionCode;
        } else if (entry.StreamType == CommentStreamA) {
            summary.comment.assign(data, entry.Location.DataSize);
        } else if (entry.StreamType == ModuleListStream) {
            ULONG32 count = 0U;
            std::memcpy(&count, data, sizeof(count));
            Require(sizeof(ULONG32) + static_cast<std::uint64_t>(count) * sizeof(MINIDUMP_MODULE) <= entry.Location.DataSize,
                "module list stream is truncated");
            for (ULONG32 item = 0U; item < count; ++item) {
                MINIDUMP_MODULE module{};
                std::memcpy(&module, data + sizeof(ULONG32) + item * sizeof(MINIDUMP_MODULE), sizeof(module));
                DumpModule parsed;
                parsed.name = ReadDumpString(bytes, module.ModuleNameRva);
                parsed.timeDateStamp = module.TimeDateStamp;
                parsed.sizeOfImage = module.SizeOfImage;
                if (module.CvRecord.DataSize > 24U) {
                    Require(static_cast<std::uint64_t>(module.CvRecord.Rva) + module.CvRecord.DataSize <= bytes.size(),
                        "CodeView record runs past the file");
                    const char* record = bytes.data() + module.CvRecord.Rva;
                    if (std::memcmp(record, "RSDS", 4U) == 0) {
                        parsed.hasPdb = true;
                        std::memcpy(&parsed.pdbGuid, record + 4, sizeof(GUID));
                        std::memcpy(&parsed.pdbAge, record + 20, sizeof(std::uint32_t));
                        parsed.pdbName.assign(record + 24, strnlen(record + 24, module.CvRecord.DataSize - 24U));
                    }
                }
                summary.modules.push_back(std::move(parsed));
            }
        } else if (entry.StreamType == UnloadedModuleListStream) {
            MINIDUMP_UNLOADED_MODULE_LIST list{};
            std::memcpy(&list, data, sizeof(list));
            Require(static_cast<std::uint64_t>(list.SizeOfHeader) + static_cast<std::uint64_t>(list.SizeOfEntry) *
                    list.NumberOfEntries <= entry.Location.DataSize && list.SizeOfEntry >= sizeof(MINIDUMP_UNLOADED_MODULE),
                "unloaded module list stream is truncated");
            for (ULONG32 item = 0U; item < list.NumberOfEntries; ++item) {
                MINIDUMP_UNLOADED_MODULE module{};
                std::memcpy(&module, data + list.SizeOfHeader + item * list.SizeOfEntry, sizeof(module));
                summary.unloadedModules.push_back(ReadDumpString(bytes, module.ModuleNameRva));
            }
        } else if (entry.StreamType == MemoryListStream) {
            ULONG32 count = 0U;
            std::memcpy(&count, data, sizeof(count));
            Require(sizeof(ULONG32) + static_cast<std::uint64_t>(count) * sizeof(MINIDUMP_MEMORY_DESCRIPTOR) <=
                    entry.Location.DataSize, "memory list stream is truncated");
            for (ULONG32 item = 0U; item < count; ++item) {
                MINIDUMP_MEMORY_DESCRIPTOR range{};
                std::memcpy(&range, data + sizeof(ULONG32) + item * sizeof(MINIDUMP_MEMORY_DESCRIPTOR), sizeof(range));
                summary.memoryRanges.emplace_back(range.Memory.Rva, range.Memory.DataSize);
            }
        }
    }
    for (std::uint32_t index = 0U; index < header.NumberOfStreams; ++index) {
        MINIDUMP_DIRECTORY entry{};
        std::memcpy(&entry, bytes.data() + header.StreamDirectoryRva + index * sizeof(MINIDUMP_DIRECTORY), sizeof(entry));
        if (entry.StreamType != ThreadListStream) {
            continue;
        }
        ULONG32 count = 0U;
        std::memcpy(&count, bytes.data() + entry.Location.Rva, sizeof(count));
        Require(sizeof(ULONG32) + static_cast<std::uint64_t>(count) * sizeof(MINIDUMP_THREAD) <= entry.Location.DataSize,
            "thread list stream is truncated");
        summary.threadCount = count;
        for (ULONG32 thread = 0U; thread < count; ++thread) {
            MINIDUMP_THREAD record{};
            std::memcpy(&record, bytes.data() + entry.Location.Rva + sizeof(ULONG32) + thread * sizeof(MINIDUMP_THREAD),
                sizeof(record));
            Require(static_cast<std::uint64_t>(record.Stack.Memory.Rva) + record.Stack.Memory.DataSize <= bytes.size(),
                "thread stack runs past the file");
            if (record.ThreadId == summary.exceptionThread && record.Stack.Memory.DataSize > 0U) {
                summary.crashedThreadHasStack = true;
            }
            summary.memoryRanges.emplace_back(record.Stack.Memory.Rva, record.Stack.Memory.DataSize);
        }
    }
    return summary;
}

[[nodiscard]] bool HasSeparator(std::wstring_view text) {
    return text.find_first_of(L"\\/") != std::wstring_view::npos;
}

[[nodiscard]] std::string Narrow(std::wstring_view text) {
    return std::filesystem::path{ text }.string();
}

// The symbol store key of an executable: time stamp (8 hex digits) and image size, upper case.
[[nodiscard]] std::string ImageId(std::uint32_t timeDateStamp, std::uint32_t sizeOfImage) {
    char text[32];
    std::snprintf(text, sizeof(text), "%08X%X", timeDateStamp, sizeOfImage);
    return text;
}

// The symbol store key of a PDB: GUID (32 hex digits) and age, upper case.
[[nodiscard]] std::string PdbId(const GUID& guid, std::uint32_t age) {
    char text[64];
    std::snprintf(text, sizeof(text), "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", guid.Data1, guid.Data2,
        guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
        guid.Data4[6], guid.Data4[7], age);
    return text;
}

// Module paths name the folders a game was installed in, often under the player's profile.
// The names alone, with the image and PDB identities, are what symbol lookup uses.
void RequireModulesWithoutPaths(const DumpSummary& dump) {
    Require(!dump.modules.empty(), "minidump has no module list");
    for (const DumpModule& module : dump.modules) {
        Require(!module.name.empty() && !HasSeparator(module.name), "minidump names a module by its path");
        Require(module.pdbName.find_first_of("\\/") == std::string::npos, "minidump names a PDB by its path");
    }
    for (const std::wstring& module : dump.unloadedModules) {
        Require(!module.empty() && !HasSeparator(module), "minidump names an unloaded module by its path");
    }
}

// The bytes of a dump that are not copies of process memory: module lists, CodeView records,
// the description, system information. Thread stacks and memory lists are blanked.
[[nodiscard]] std::string DumpMetadataBytes(const std::string& bytes, const DumpSummary& dump) {
    std::string metadata = bytes;
    for (const auto& [rva, size] : dump.memoryRanges) {
        Require(rva + size <= metadata.size(), "minidump memory range runs past the file");
        std::fill_n(metadata.begin() + static_cast<std::ptrdiff_t>(rva), static_cast<std::ptrdiff_t>(size), '\0');
    }
    return metadata;
}

// `needle` in `haystack` as UTF-8 or UTF-16LE, ignoring ASCII case.
[[nodiscard]] bool ContainsText(const std::string& haystack, std::string_view needle) {
    const std::string lowered = Lower(haystack);
    std::string narrow = Lower(std::string{ needle });
    std::string wide;
    for (const char character : narrow) {
        wide.push_back(character);
        wide.push_back('\0');
    }
    return lowered.find(narrow) != std::string::npos || lowered.find(wide) != std::string::npos;
}

struct CrashCase {
    std::wstring_view mode;
    std::string_view reason;
    std::uint32_t code;
};

void CheckCrashReport(const CrashCase& crash) {
    std::string modeName;
    for (const wchar_t character : crash.mode) {
        modeName.push_back(static_cast<char>(character));
    }
    const std::filesystem::path directory = FreshDirectory("crash-" + modeName);
    const HelperRun run = RunHelper(directory, crash.mode);
    Require(run.exitCode != 0U, "a crashing helper reported success");

    const std::vector<kb::platform::CrashReportFiles> reports = kb::platform::ListCrashReports(directory);
    if (reports.size() != 1U) {
        std::cerr << "mode " << modeName << " exit 0x" << std::hex << run.exitCode << std::dec
                  << " left " << reports.size() << " reports; output: " << run.output << '\n';
    }
    Require(reports.size() == 1U, "a crash did not leave exactly one complete report");

    const DumpSummary dump = ReadMinidump(reports.front().minidump);
    for (const std::uint32_t stream : { static_cast<std::uint32_t>(ThreadListStream),
             static_cast<std::uint32_t>(ModuleListStream), static_cast<std::uint32_t>(ExceptionStream),
             static_cast<std::uint32_t>(SystemInfoStream), static_cast<std::uint32_t>(CommentStreamA) }) {
        Require(dump.streams.contains(stream), "minidump lacks a thread, module, exception, system or comment stream");
    }
    // The helper's own thread and the reporter thread at least.
    Require(dump.threadCount >= 2U, "minidump does not carry every thread");
    Require(dump.crashedThreadHasStack, "minidump does not carry the crashed thread's stack");
    Require(dump.exceptionCode == crash.code, "minidump exception record names the wrong failure");

    const std::string metadataText = ReadBytes(reports.front().metadata);
    Require(dump.comment == metadataText, "minidump comment stream differs from the description beside it");
    kb::core::JsonValue metadata;
    std::string error;
    Require(kb::core::JsonValue::Parse(metadataText, metadata, error), "crash description is not JSON");
    const auto text = [&](std::string_view key) -> std::string {
        const kb::core::JsonValue* value = metadata.Find(key);
        return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? value->AsString() : std::string{};
    };
    Require(text("product") == "Crash Test Helper" && text("version") == "1.2.3",
        "crash description does not name the product and version");
    Require(text("reason") == crash.reason, "crash description names the wrong reason");
    Require(text("executable") == "kb_crash_reporting_test_helper.exe", "crash description does not name the executable");
    // The helper links with a PDB in every configuration, as shipped binaries do.
    Require(text("buildId").size() > 32U, "crash description does not carry the executable's symbol identity");
    Require(text("minidump") == reports.front().minidump.filename().string(), "crash description does not name its dump");
    Require(text("timestampUtc").size() == 20U && text("timestampUtc").back() == 'Z', "crash description has no UTC time");
    const kb::core::JsonValue* threadId = metadata.Find("threadId");
    Require(threadId != nullptr && static_cast<std::uint32_t>(threadId->AsNumber()) == dump.exceptionThread,
        "crash description and dump disagree on the crashed thread");

    const kb::core::JsonValue* modules = metadata.Find("modules");
    Require(modules != nullptr && modules->Size() >= 3U, "crash description has no module list");
    bool sawHelper = false;
    bool sawIdentifiedSystemModule = false;
    for (std::size_t index = 0U; index < modules->Size(); ++index) {
        const kb::core::JsonValue* name = modules->At(index)->Find("name");
        Require(name != nullptr && name->AsString().find_first_of("\\/") == std::string::npos,
            "crash description lists a module by path instead of by name");
        sawHelper = sawHelper || name->AsString() == "kb_crash_reporting_test_helper.exe";
        const kb::core::JsonValue* pdbId = modules->At(index)->Find("pdbId");
        if (Lower(name->AsString()) == "ntdll.dll" && pdbId != nullptr && pdbId->AsString().size() > 32U) {
            sawIdentifiedSystemModule = true;
        }
    }
    Require(sawHelper && sawIdentifiedSystemModule, "crash description misses modules or their symbol identities");

    // The dump identifies every module the way the description does, by name and symbol keys.
    RequireModulesWithoutPaths(dump);
    for (std::size_t index = 0U; index < modules->Size(); ++index) {
        const kb::core::JsonValue& entry = *modules->At(index);
        const std::string name = entry.Find("name")->AsString();
        const auto inDump = std::ranges::find_if(dump.modules, [&](const DumpModule& module) {
            return Lower(Narrow(module.name)) == Lower(name);
        });
        Require(inDump != dump.modules.end(), "a described module is missing from the dump");
        const kb::core::JsonValue* imageId = entry.Find("imageId");
        Require(imageId != nullptr && imageId->AsString() == ImageId(inDump->timeDateStamp, inDump->sizeOfImage),
            "crash description and dump disagree on a module's image identity");
        if (const kb::core::JsonValue* pdbId = entry.Find("pdbId"); pdbId != nullptr) {
            Require(inDump->hasPdb && pdbId->AsString() == PdbId(inDump->pdbGuid, inDump->pdbAge) &&
                    entry.Find("pdb")->AsString() == inDump->pdbName,
                "crash description and dump disagree on a module's PDB identity");
        }
    }

    const kb::core::JsonValue* log = metadata.Find("log");
    Require(log != nullptr && log->Size() >= 2U && log->At(0)->AsString() == "helper: before the crash",
        "crash description does not carry the recent log");
    Require(log->At(1)->AsString() == "helper: opened %USERPROFILE%\\Documents\\save.dat",
        "crash description does not replace the user profile in logged paths");
    wchar_t userName[256];
    const DWORD userNameLength = GetEnvironmentVariableW(L"USERNAME", userName, 256U);
    if (userNameLength >= 3U && userNameLength < 256U) {
        Require(log->Size() >= 3U && log->At(2)->AsString() == "helper: cache in D:\\Shared\\%USERNAME%\\cache.bin",
            "crash description does not replace the account name in logged paths");
    }
    wchar_t profile[1024];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, 1024U);
    if (length != 0U && length < 1024U) {
        const std::string narrow = std::filesystem::path{ profile }.string();
        Require(Lower(metadataText).find(Lower(narrow)) == std::string::npos &&
                Lower(metadataText).find(Lower(std::filesystem::path{ profile }.generic_string())) == std::string::npos,
            "crash description names the user profile");
    }
}

void RunCrashKindsTest() {
    // Each failure the reporter claims to catch, with the exception code the dump
    // must carry: the structured exceptions keep theirs, the rest get the
    // reporter's own.
    const CrashCase cases[] = {
        { L"access-violation", "unhandled-exception", EXCEPTION_ACCESS_VIOLATION },
        { L"stack-overflow", "unhandled-exception", EXCEPTION_STACK_OVERFLOW },
        { L"terminate", "std-terminate: helper terminate reason", 0xE0216B01U },
        { L"abort", "abort", 0xE0216B02U },
        { L"pure-call", "pure-virtual-call", 0xE0216B03U },
        { L"invalid-parameter", "invalid-parameter", 0xE0216B04U },
        { L"thread-abort", "abort", 0xE0216B02U },
    };
    for (const CrashCase& crash : cases) {
        CheckCrashReport(crash);
    }
}

void RunInstallCostTest() {
    const std::filesystem::path directory = FreshDirectory("timing");
    const HelperRun run = RunHelper(directory, L"timing");
    Require(run.exitCode == 0U, "timing helper failed");
    long long installMicroseconds = -1;
    double noteNanoseconds = -1.0;
    const std::size_t installAt = run.output.find("install_us=");
    const std::size_t noteAt = run.output.find(" note_ns=");
    Require(installAt != std::string::npos && noteAt != std::string::npos, "timing helper did not report");
    installMicroseconds = std::stoll(run.output.substr(installAt + 11U, noteAt - installAt - 11U));
    noteNanoseconds = std::stod(run.output.substr(noteAt + 9U));
    std::cout << "crash reporter install " << installMicroseconds << " us, note " << noteNanoseconds << " ns\n";
    // The handlers and one thread; everything slow happens on that thread.
    Require(installMicroseconds < static_cast<long long>(10'000.0 * kb::tests::kSanitizerTimeScale),
        "installing the crash reporter costs measurable startup time");
    Require(noteNanoseconds < 1'000.0 * kb::tests::kSanitizerTimeScale, "a crash log note is not cheap enough for a frame path");
    Require(kb::platform::ListCrashReports(directory).empty(), "a run that did not crash left a report");
}

class LoopbackServer final {
public:
    explicit LoopbackServer(int status) : status_(status) {
        WSADATA data{};
        Require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock could not start");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(listener_ != INVALID_SOCKET, "loopback socket could not be created");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int addressLength = sizeof(address);
        Require(bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                listen(listener_, 4) == 0 &&
                getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &addressLength) == 0,
            "loopback server could not listen");
        port_ = ntohs(address.sin_port);
        thread_ = std::thread{ [this] { Serve(); } };
    }
    ~LoopbackServer() {
        closesocket(listener_);
        thread_.join();
        WSACleanup();
    }
    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    [[nodiscard]] std::string Url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/submit?key=test"; }
    [[nodiscard]] std::vector<std::string> Requests() {
        std::scoped_lock lock{ mutex_ };
        return requests_;
    }

private:
    void Serve() {
        for (;;) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;
            }
            std::string request;
            char buffer[65536];
            std::size_t expected = std::string::npos;
            for (;;) {
                const int received = recv(client, buffer, sizeof(buffer), 0);
                if (received <= 0) {
                    break;
                }
                request.append(buffer, static_cast<std::size_t>(received));
                const std::size_t headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos && expected == std::string::npos) {
                    const std::string headers = Lower(request.substr(0U, headerEnd));
                    const std::size_t length = headers.find("content-length:");
                    expected = headerEnd + 4U + (length == std::string::npos ? 0U :
                        static_cast<std::size_t>(std::stoull(headers.substr(length + 15U))));
                }
                if (expected != std::string::npos && request.size() >= expected) {
                    break;
                }
            }
            {
                std::scoped_lock lock{ mutex_ };
                requests_.push_back(request);
            }
            const std::string response = "HTTP/1.1 " + std::to_string(status_) +
                (status_ == 200 ? " OK" : " Error") + "\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
            send(client, response.data(), static_cast<int>(response.size()), 0);
            shutdown(client, SD_SEND);
            closesocket(client);
        }
    }

    int status_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0U;
    std::thread thread_;
    std::mutex mutex_;
    std::vector<std::string> requests_;
};

// The content of the multipart part called `name` in a captured upload request.
[[nodiscard]] std::string UploadedPart(const std::string& request, std::string_view name) {
    const std::string lowered = Lower(request.substr(0U, request.find("\r\n\r\n")));
    const std::string marker = "content-type: multipart/form-data; boundary=";
    const std::size_t boundaryAt = lowered.find(marker);
    Require(boundaryAt != std::string::npos, "the upload is not multipart/form-data");
    const std::size_t boundaryStart = boundaryAt + marker.size();
    const std::string boundary = request.substr(boundaryStart, request.find("\r\n", boundaryStart) - boundaryStart);
    const std::string header = "Content-Disposition: form-data; name=\"" + std::string{ name } + "\"";
    const std::size_t partAt = request.find(header);
    Require(partAt != std::string::npos, "the upload lacks a part");
    const std::size_t contentAt = request.find("\r\n\r\n", partAt) + 4U;
    const std::size_t contentEnd = request.find("\r\n--" + boundary, contentAt);
    Require(contentEnd != std::string::npos, "an upload part is not terminated");
    return request.substr(contentAt, contentEnd - contentAt);
}

[[nodiscard]] std::filesystem::path PrepareReportDirectory(std::string_view name) {
    // One real report, written by a crashing helper, to send.
    const std::filesystem::path directory = FreshDirectory(name);
    static_cast<void>(RunHelper(directory, L"access-violation"));
    Require(kb::platform::ListCrashReports(directory).size() == 1U, "upload fixture report was not written");
    return directory;
}

[[nodiscard]] std::wstring EnvironmentWith(std::initializer_list<std::pair<std::wstring_view, std::wstring>> overrides) {
    std::wstring block;
    const LPWCH current = GetEnvironmentStringsW();
    Require(current != nullptr, "the environment could not be read");
    for (const wchar_t* entry = current; *entry != L'\0'; entry += std::wcslen(entry) + 1U) {
        const std::wstring_view text{ entry };
        const bool replaced = std::ranges::any_of(overrides, [&](const auto& item) {
            return text.size() > item.first.size() && text[item.first.size()] == L'=' &&
                _wcsnicmp(text.data(), item.first.data(), item.first.size()) == 0;
        });
        if (!replaced) {
            block.append(text);
            block.push_back(L'\0');
        }
    }
    FreeEnvironmentStringsW(current);
    for (const auto& [name, value] : overrides) {
        block.append(name);
        block.push_back(L'=');
        block.append(value);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

// A game installed inside the player's profile: neither the profile, nor the account name, nor
// the install folder may reach the description or the uploaded dump's metadata.
void RunInstalledUnderProfilePrivacyTest() {
    constexpr std::wstring_view kAccount = L"KbPrivacyAccount";
    const std::filesystem::path profile = FreshDirectory("privacy") / L"Users" / kAccount;
    const std::filesystem::path install = profile / L"Games" / L"Crash Test";
    std::filesystem::create_directories(install);
    const std::filesystem::path helper = install / std::filesystem::path{ KB_CRASH_REPORTING_TEST_HELPER }.filename();
    std::filesystem::copy_file(KB_CRASH_REPORTING_TEST_HELPER, helper, std::filesystem::copy_options::overwrite_existing);
    const std::filesystem::path reports = profile / L"AppData" / L"Local" / L"21kb" / L"CrashReports" / L"Helper";
    const HelperRun run = RunHelper(reports, L"access-violation", helper,
        EnvironmentWith({ { L"USERPROFILE", profile.wstring() }, { L"USERNAME", std::wstring{ kAccount } } }));
    Require(run.exitCode != 0U, "the crashing helper copy reported success");
    const std::vector<kb::platform::CrashReportFiles> written = kb::platform::ListCrashReports(reports);
    Require(written.size() == 1U, "the helper installed under a profile did not leave a report");

    const std::string account = Narrow(kAccount);
    const std::string metadataText = ReadBytes(written.front().metadata);
    Require(!ContainsText(metadataText, account), "the crash description names the player's account");
    kb::core::JsonValue metadata;
    std::string error;
    Require(kb::core::JsonValue::Parse(metadataText, metadata, error), "crash description is not JSON");
    const kb::core::JsonValue* log = metadata.Find("log");
    Require(log != nullptr && log->Size() >= 3U &&
            log->At(1)->AsString() == "helper: opened %USERPROFILE%\\Documents\\save.dat" &&
            log->At(2)->AsString() == "helper: cache in D:\\Shared\\%USERNAME%\\cache.bin",
        "the crash description does not stand in for the profile and the account name");

    const std::string dumpBytes = ReadBytes(written.front().minidump);
    const DumpSummary dump = ReadMinidumpBytes(dumpBytes);
    RequireModulesWithoutPaths(dump);
    Require(!ContainsText(DumpMetadataBytes(dumpBytes, dump), account),
        "the minidump names the player's account outside of process memory");

    Require(kb::platform::SetCrashUploadConsent(reports, true), "upload consent could not be given");
    LoopbackServer server{ 200 };
    const kb::platform::CrashUploadSummary summary = kb::platform::UploadPendingCrashReports(reports, server.Url());
    Require(summary.uploaded == 1U && server.Requests().size() == 1U, "the report was not uploaded");
    const std::string uploadedDump = UploadedPart(server.Requests().front(), "upload_file_minidump");
    const std::string uploadedMetadata = UploadedPart(server.Requests().front(), "metadata");
    const DumpSummary uploaded = ReadMinidumpBytes(uploadedDump);
    RequireModulesWithoutPaths(uploaded);
    Require(!ContainsText(DumpMetadataBytes(uploadedDump, uploaded), account) && !ContainsText(uploadedMetadata, account),
        "the upload names the player's account outside of process memory");
}

// A report written before module paths were stripped on disk: the upload strips them and keeps
// every module's identity.
void RunLegacyDumpUploadTest() {
    const std::filesystem::path directory = FreshDirectory("legacy-upload");
    const std::filesystem::path dumpPath = directory / "crash-20260101-000000-1.dmp";
    {
        const HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "legacy dump file could not be created");
        const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithUnloadedModules), nullptr, nullptr, nullptr);
        CloseHandle(file);
        Require(written != FALSE, "legacy dump could not be written");
    }
    std::ofstream{ directory / "crash-20260101-000000-1.json", std::ios::binary }
        << "{\"schema\":\"21kb.crash-report/2\",\"product\":\"Legacy\",\"version\":\"1\",\"buildId\":null}\n";
    const std::string original = ReadBytes(dumpPath);
    const DumpSummary before = ReadMinidumpBytes(original);
    Require(std::ranges::any_of(before.modules, [](const DumpModule& module) { return HasSeparator(module.name); }),
        "the legacy fixture does not carry module paths");

    Require(kb::platform::SetCrashUploadConsent(directory, true), "upload consent could not be given");
    LoopbackServer server{ 200 };
    const kb::platform::CrashUploadSummary summary = kb::platform::UploadPendingCrashReports(directory, server.Url());
    Require(summary.uploaded == 1U && server.Requests().size() == 1U, "the legacy report was not uploaded");
    const std::string uploadedDump = UploadedPart(server.Requests().front(), "upload_file_minidump");
    Require(uploadedDump.size() == original.size(), "stripping module paths changed the dump's size");
    const DumpSummary after = ReadMinidumpBytes(uploadedDump);
    RequireModulesWithoutPaths(after);
    Require(after.modules.size() == before.modules.size(), "the upload lost modules");
    for (std::size_t index = 0U; index < before.modules.size(); ++index) {
        const DumpModule& left = before.modules[index];
        const DumpModule& right = after.modules[index];
        Require(right.name == std::filesystem::path{ left.name }.filename().wstring() &&
                right.timeDateStamp == left.timeDateStamp && right.sizeOfImage == left.sizeOfImage &&
                right.hasPdb == left.hasPdb && std::memcmp(&right.pdbGuid, &left.pdbGuid, sizeof(GUID)) == 0 &&
                right.pdbAge == left.pdbAge && right.pdbName == std::filesystem::path{ left.pdbName }.filename().string(),
            "the upload changed a module's identity");
    }
}

// A crash is symbolized from the description alone: the module's file name, image identity
// and PDB identity find the exact build in a symbol folder, whatever folder the game ran from.
void RunSymbolizationFromDescriptionTest() {
    const std::filesystem::path directory = FreshDirectory("symbolize");
    const std::filesystem::path install = directory / "install";
    std::filesystem::create_directories(install);
    const std::filesystem::path built{ KB_CRASH_REPORTING_TEST_HELPER };
    const std::filesystem::path helper = install / built.filename();
    std::filesystem::copy_file(built, helper);
    const std::filesystem::path reports = directory / "reports";
    static_cast<void>(RunHelper(reports, L"access-violation", helper));
    const std::vector<kb::platform::CrashReportFiles> written = kb::platform::ListCrashReports(reports);
    Require(written.size() == 1U, "the symbolization fixture report was not written");

    kb::core::JsonValue metadata;
    std::string error;
    Require(kb::core::JsonValue::Parse(ReadBytes(written.front().metadata), metadata, error), "crash description is not JSON");
    const kb::core::JsonValue* exception = metadata.Find("exception");
    Require(exception != nullptr && exception->Find("module") != nullptr && exception->Find("offset") != nullptr,
        "crash description does not locate the fault");
    const std::string faultModule = exception->Find("module")->AsString();
    const std::uint64_t faultOffset = std::stoull(exception->Find("offset")->AsString(), nullptr, 16);
    const kb::core::JsonValue* modules = metadata.Find("modules");
    const kb::core::JsonValue* entry = nullptr;
    for (std::size_t index = 0U; modules != nullptr && index < modules->Size(); ++index) {
        if (modules->At(index)->Find("name")->AsString() == faultModule) {
            entry = modules->At(index);
        }
    }
    Require(faultModule == built.filename().string() && entry != nullptr && entry->Find("pdbId") != nullptr,
        "the fault is not described in the helper's own module");
    const std::uint64_t base = std::stoull(entry->Find("base")->AsString(), nullptr, 16);
    const auto size = static_cast<DWORD>(entry->Find("size")->AsNumber());

    // The symbol folder holds the build's image and PDB; nothing points at where it was built.
    const std::filesystem::path symbols = directory / "symbols";
    std::filesystem::create_directories(symbols);
    std::filesystem::copy_file(built, symbols / built.filename());
    std::filesystem::path pdb = built;
    pdb.replace_extension(".pdb");
    Require(std::filesystem::is_regular_file(pdb), "the helper was linked without a PDB");
    std::filesystem::copy_file(pdb, symbols / entry->Find("pdb")->AsString());

    // The image identity in the description is the one of the build in the symbol folder.
    const std::string image = ReadBytes(symbols / built.filename());
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, image.data(), sizeof(dos));
    IMAGE_NT_HEADERS nt{};
    std::memcpy(&nt, image.data() + dos.e_lfanew, sizeof(nt));
    Require(entry->Find("imageId")->AsString() == ImageId(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage),
        "the described image identity does not match the build");

    const HANDLE session = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x21CBU));
    SymSetOptions(SYMOPT_IGNORE_CVREC | SYMOPT_EXACT_SYMBOLS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS |
        SYMOPT_NO_PROMPTS);
    Require(SymInitializeW(session, symbols.c_str(), FALSE) != FALSE, "dbghelp could not start a symbol session");
    const DWORD64 loaded = SymLoadModuleExW(session, nullptr, (symbols / built.filename()).c_str(), nullptr, base, size,
        nullptr, 0U);
    IMAGEHLP_MODULEW64 info{};
    info.SizeOfStruct = sizeof(info);
    const bool haveInfo = loaded != 0U && SymGetModuleInfoW64(session, base, &info) != FALSE;
    alignas(SYMBOL_INFOW) std::byte storage[sizeof(SYMBOL_INFOW) + 256U * sizeof(wchar_t)]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = 256U;
    DWORD64 displacement = 0U;
    const bool resolved = haveInfo && SymFromAddrW(session, base + faultOffset, &displacement, symbol) != FALSE;
    const std::wstring function = resolved ? std::wstring{ symbol->Name, symbol->NameLen } : std::wstring{};
    SymCleanup(session);
    Require(haveInfo && info.SymType == SymPdb, "the described PDB identity did not load the build's symbols");
    Require(PdbId(info.PdbSig70, info.PdbAge) == entry->Find("pdbId")->AsString(),
        "the loaded symbols are not the ones the description names");
    Require(resolved && function == L"wmain", "the fault address did not symbolize to the crashing function");
}

void RunEndpointPolicyTest() {
    using kb::platform::IsAllowedCrashUploadEndpoint;
    Require(IsAllowedCrashUploadEndpoint("https://crash.example.com/api/1/minidump/?sentry_key=abc"),
        "an HTTPS endpoint was refused");
    Require(IsAllowedCrashUploadEndpoint("http://127.0.0.1:8080/submit") &&
            IsAllowedCrashUploadEndpoint("http://[::1]:8080/submit"),
        "a loopback HTTP endpoint was refused");
    for (const std::string_view refused : { "http://crash.example.com/submit", "http://localhost:8080/submit",
             "http://127.0.0.2/submit", "ftp://crash.example.com/", "https://user:secret@crash.example.com/",
             "", "https://crash.example.com/a b", "file:///C:/Windows/win.ini", "crash.example.com/submit" }) {
        Require(!IsAllowedCrashUploadEndpoint(refused), "an endpoint that is neither HTTPS nor loopback was allowed");
    }

    const std::filesystem::path directory = FreshDirectory("config");
    const std::filesystem::path config = directory / "CrashReports.ini";
    std::ofstream{ config } << "; written by the packager\n[Other]\nUploadUrl=https://wrong.example.com/\n"
                               "[CrashReports]\nUploadUrl = https://crash.example.com/submit \n";
    Require(kb::platform::ReadCrashUploadEndpoint(config) == "https://crash.example.com/submit",
        "the endpoint was not read from its section");
    std::ofstream{ config } << "[CrashReports]\nUploadUrl=http://crash.example.com/submit\n";
    Require(kb::platform::ReadCrashUploadEndpoint(config).empty(), "a plain HTTP endpoint was read as usable");
}

void RunUploadTest() {
    const std::filesystem::path directory = PrepareReportDirectory("upload");
    const kb::platform::CrashReportFiles report = kb::platform::ListCrashReports(directory).front();
    const std::string dump = ReadBytes(report.minidump);
    Require(kb::platform::SetCrashUploadConsent(directory, true), "upload consent could not be given");

    {
        LoopbackServer failing{ 500 };
        const kb::platform::CrashUploadSummary summary = kb::platform::UploadPendingCrashReports(directory, failing.Url());
        Require(summary.attempted && summary.uploaded == 0U && summary.failed == 1U, "a rejected upload counted as sent");
        Require(failing.Requests().size() == 1U, "the failing endpoint was not asked exactly once");
        Require(kb::platform::ListCrashReports(directory).size() == 1U, "a rejected report was deleted");
    }

    LoopbackServer server{ 200 };
    const kb::platform::CrashUploadSummary summary = kb::platform::UploadPendingCrashReports(directory, server.Url());
    if (summary.uploaded != 1U) {
        std::cerr << "upload error: " << summary.error << '\n';
    }
    Require(summary.attempted && summary.uploaded == 1U && summary.failed == 0U, "a consented report was not uploaded");
    const std::vector<std::string> requests = server.Requests();
    Require(requests.size() == 1U, "the endpoint did not receive exactly one request");
    const std::string& request = requests.front();
    Require(request.starts_with("POST /submit?key=test HTTP/1.1\r\n"), "the upload was not a POST to the configured path");
    const std::string lowered = Lower(request.substr(0U, request.find("\r\n\r\n")));
    const std::size_t boundaryAt = lowered.find("content-type: multipart/form-data; boundary=");
    Require(boundaryAt != std::string::npos, "the upload is not multipart/form-data");
    const std::size_t boundaryStart = boundaryAt + std::strlen("content-type: multipart/form-data; boundary=");
    const std::string boundary = request.substr(boundaryStart, request.find("\r\n", boundaryStart) - boundaryStart);
    const std::string body = request.substr(request.find("\r\n\r\n") + 4U);
    Require(body.starts_with("--" + boundary + "\r\n") && body.ends_with("--" + boundary + "--\r\n"),
        "the multipart body is not framed by its boundary");
    Require(body.find("name=\"prod\"\r\n\r\nCrash Test Helper\r\n") != std::string::npos &&
            body.find("name=\"ver\"\r\n\r\n1.2.3\r\n") != std::string::npos,
        "the upload lacks the product and version fields");
    const std::string dumpHeader = "name=\"upload_file_minidump\"; filename=\"" + report.minidump.filename().string() +
        "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    const std::size_t dumpAt = body.find(dumpHeader);
    Require(dumpAt != std::string::npos && body.compare(dumpAt + dumpHeader.size(), dump.size(), dump) == 0,
        "the upload does not carry the minidump unchanged");
    Require(body.find("name=\"metadata\"; filename=\"metadata.json\"") != std::string::npos,
        "the upload does not carry the description");
    Require(kb::platform::ListCrashReports(directory).empty() && !std::filesystem::exists(report.minidump),
        "an accepted report was kept on disk");
    Require(kb::platform::HasCrashUploadConsent(directory), "uploading changed the consent choice");
}

void RunConsentGateTest() {
    const std::filesystem::path directory = PrepareReportDirectory("upload-consent");
    Require(!kb::platform::HasCrashUploadConsent(directory), "upload consent is not off by default");

    LoopbackServer server{ 200 };
    const kb::platform::CrashUploadSummary refused = kb::platform::UploadPendingCrashReports(directory, server.Url());
    Require(!refused.attempted && refused.uploaded == 0U && !refused.error.empty(),
        "reports were offered for upload without consent");
    Require(server.Requests().empty(), "a report reached the network without consent");
    Require(kb::platform::ListCrashReports(directory).size() == 1U, "a refused upload touched the local report");

    Require(kb::platform::SetCrashUploadConsent(directory, true) && kb::platform::HasCrashUploadConsent(directory),
        "upload consent could not be given");
    Require(kb::platform::SetCrashUploadConsent(directory, false) && !kb::platform::HasCrashUploadConsent(directory),
        "upload consent could not be withdrawn");
    static_cast<void>(kb::platform::UploadPendingCrashReports(directory, server.Url()));
    Require(server.Requests().empty(), "a report reached the network after consent was withdrawn");

    // Anything but the exact consent record is no consent.
    std::ofstream{ directory / "upload-consent" } << "upload=YES\n";
    Require(!kb::platform::HasCrashUploadConsent(directory), "a malformed consent record counted as consent");
}

void RunDeleteReportsTest() {
    const std::filesystem::path directory = PrepareReportDirectory("delete");
    Require(kb::platform::SetCrashUploadConsent(directory, true), "upload consent could not be given");
    // A dump whose description was never written is incomplete: not listed, still deleted.
    std::ofstream{ directory / "crash-20260101-000000-1.dmp", std::ios::binary } << "MDMP";
    std::ofstream{ directory / "notes.txt" } << "not a report";
    Require(kb::platform::ListCrashReports(directory).size() == 1U, "an incomplete report was listed");
    Require(kb::platform::DeleteCrashReports(directory) == 3U, "not every report file was deleted");
    Require(kb::platform::ListCrashReports(directory).empty(), "reports survived deletion");
    Require(std::filesystem::exists(directory / "notes.txt"), "deleting reports removed an unrelated file");
    Require(kb::platform::HasCrashUploadConsent(directory), "deleting reports changed the consent choice");
    Require(kb::platform::DeleteCrashReports(directory / "missing") == 0U, "deleting from a missing directory failed");
}

void RunPrivacyNoticeTest() {
    const std::filesystem::path directory = FreshDirectory("notice");
    Require(kb::platform::ReadCrashReportPrivacyNotice(directory).empty(), "a missing privacy notice read as text");
    std::ofstream{ directory / "CRASH_REPORTS.txt", std::ios::binary } << "Crash reports\nWhat a report holds.\n";
    Require(kb::platform::ReadCrashReportPrivacyNotice(directory) == "Crash reports\nWhat a report holds.\n",
        "the shipped privacy notice was not read verbatim");
    // The one that ships: the packager copies it beside every Windows player.
    const std::filesystem::path shipped = std::filesystem::path{ KB_CRASH_REPORTING_PRIVACY_NOTICE };
    const std::string notice = kb::platform::ReadCrashReportPrivacyNotice(shipped.parent_path());
    for (const std::string_view promise : { "%LOCALAPPDATA%", ".dmp", ".json", "%USERPROFILE%", "%USERNAME%",
             "by file name and version only", "HTTPS", "off until" }) {
        Require(notice.find(promise) != std::string::npos, "the shipped privacy notice does not describe what is collected and sent");
    }
}

// The player's choices reach the game through the Settings script surface, which
// acts on the reporter this process installed.
void RunSettingsScriptSurfaceTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    Require(host.Succeeded(), "script host did not initialize");
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const auto call = [&](std::string_view name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
        return host.Functions().Call(name, arguments, context);
    };
    const auto consentArgument = [](bool consent) {
        return kb::script::ScriptFunctionArgument{ .name = "consent", .value = kb::script::ScriptValue{ consent } };
    };
    Require(!call("Settings.CrashReportUploadConsent", {}).Output("consent")->AsBool() &&
            !call("Settings.SetCrashReportUploadConsent", { consentArgument(true) }).Output("set")->AsBool() &&
            call("Settings.DeleteCrashReports", {}).Output("deleted")->AsInt() == 0,
        "crash report settings acted without an installed reporter");

    const std::filesystem::path directory = PrepareReportDirectory("settings");
    kb::platform::CrashReporterOptions options;
    options.productName = "Crash Reporting Tests";
    options.reportDirectory = directory;
    options.uploadPendingReports = false;
    Require(kb::platform::CrashReporter::Install(options), "the test process could not install its reporter");

    Require(!call("Settings.CrashReportUploadConsent", {}).Output("consent")->AsBool(), "upload consent is not off by default");
    Require(call("Settings.SetCrashReportUploadConsent", { consentArgument(true) }).Output("set")->AsBool() &&
            call("Settings.CrashReportUploadConsent", {}).Output("consent")->AsBool() &&
            kb::platform::HasCrashUploadConsent(directory),
        "a script could not record the player's consent");
    Require(call("Settings.SetCrashReportUploadConsent", { consentArgument(false) }).Output("set")->AsBool() &&
            !kb::platform::HasCrashUploadConsent(directory),
        "a script could not withdraw the player's consent");
    Require(call("Settings.DeleteCrashReports", {}).Output("deleted")->AsInt() == 2 &&
            kb::platform::ListCrashReports(directory).empty(),
        "a script could not delete the stored reports");
    const kb::script::ScriptFunctionCallResult notice = call("Settings.CrashReportPrivacyNotice", {});
    Require(notice.Succeeded() && notice.Output("text").has_value(), "the privacy notice is not readable from a script");
}

void RunDefaultDirectoryTest() {
    const std::filesystem::path directory = kb::platform::DefaultCrashReportDirectory(L"My:Game");
    Require(!directory.empty() && directory.filename() == L"My_Game" &&
            directory.parent_path().filename() == L"CrashReports",
        "the default report directory is not a per-user folder named after the executable");
    wchar_t local[1024];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 1024U);
    if (length != 0U && length < 1024U) {
        Require(Lower(directory.string()).starts_with(Lower(std::filesystem::path{ local }.string())),
            "the default report directory is not under the user's local application data");
    }
}

} // namespace

int main() {
    // A crash the reporter misses must fail the test, not wait behind an error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::filesystem::create_directories(kRoot);
    RunCrashKindsTest();
    RunInstallCostTest();
    RunEndpointPolicyTest();
    RunConsentGateTest();
    RunUploadTest();
    RunDeleteReportsTest();
    RunInstalledUnderProfilePrivacyTest();
    RunLegacyDumpUploadTest();
    RunSymbolizationFromDescriptionTest();
    RunPrivacyNoticeTest();
    RunDefaultDirectoryTest();
    // Last: it installs the reporter in this process.
    RunSettingsScriptSurfaceTest();
    std::cout << "crash reporting tests passed\n";
    return 0;
}
