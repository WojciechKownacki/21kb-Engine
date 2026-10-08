// Crash capture end to end: a helper process installs the reporter and fails in
// each supported way; the report it leaves is checked as a minidump and as JSON.
// Upload is checked against a loopback HTTP server owned by this test.

#include "engine/core/JsonValue.hpp"
#include "engine/platform/CrashReporting.hpp"

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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
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

[[nodiscard]] HelperRun RunHelper(const std::filesystem::path& directory, std::wstring_view mode) {
    SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    Require(CreatePipe(&readPipe, &writePipe, &inheritable, 0U) != FALSE &&
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0U) != FALSE, "helper output pipe could not be created");
    std::wstring command = L"\"" + std::filesystem::path{ KB_CRASH_REPORTING_TEST_HELPER }.wstring() + L"\" \"" +
        directory.wstring() + L"\" " + std::wstring{ mode };
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);
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

struct DumpSummary {
    std::set<std::uint32_t> streams;
    std::uint32_t threadCount = 0U;
    std::uint32_t exceptionThread = 0U;
    std::uint32_t exceptionCode = 0U;
    bool crashedThreadHasStack = false;
    std::string comment;
};

// Validates the container by hand rather than through dbghelp, so the test
// checks the bytes a symbol server or crash backend would receive.
[[nodiscard]] DumpSummary ReadMinidump(const std::filesystem::path& path) {
    const std::string bytes = ReadBytes(path);
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
        }
    }
    return summary;
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

    const kb::core::JsonValue* log = metadata.Find("log");
    Require(log != nullptr && log->Size() >= 2U && log->At(0)->AsString() == "helper: before the crash",
        "crash description does not carry the recent log");
    Require(log->At(1)->AsString() == "helper: opened %USERPROFILE%\\Documents\\save.dat",
        "crash description does not replace the user profile in logged paths");
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

[[nodiscard]] std::filesystem::path PrepareReportDirectory(std::string_view name) {
    // One real report, written by a crashing helper, to send.
    const std::filesystem::path directory = FreshDirectory(name);
    static_cast<void>(RunHelper(directory, L"access-violation"));
    Require(kb::platform::ListCrashReports(directory).size() == 1U, "upload fixture report was not written");
    return directory;
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
    RunUploadTest();
    RunDefaultDirectoryTest();
    std::cout << "crash reporting tests passed\n";
    return 0;
}
