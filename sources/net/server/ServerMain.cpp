#include "DedicatedServer.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::atomic<bool> g_stop{ false };

#if defined(_WIN32)
BOOL WINAPI ConsoleHandler(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_stop.store(true);
        return TRUE;
    default:
        return FALSE;
    }
}
#else
void SignalHandler(int) {
    g_stop.store(true);
}
#endif

constexpr const char* kUsage = R"(kb_dedicated_server - run a project's scene headless and replicate it to clients over UDP

usage: kb_dedicated_server --project <dir> --keys <file> [options]

  --project <dir>          project directory (required)
  --keys <file>            client keys, one "<client id> <64 hex digits>" per line, '#' starts a comment (required)
  --scene <path>           scene to run (default: the project's startup scene)
  --port <n>               UDP port, 0 lets the system choose (default 27015)
  --bind <ipv4>            address to listen on (default 0.0.0.0)
  --max-clients <n>        connected clients allowed (default 16)
  --tick-rate <hz>         fixed simulation and replication steps per second, 10 to 240 (default 30)
  --idle-timeout <s>       drop a client silent for this many seconds (default 30)
  --empty-timeout <s>      stop after this many seconds without a client, once one has connected (default: never)
  --max-seconds <s>        stop after this many seconds (default: run until stopped)
  --replicate <mode>       all (every entity with a transform), none, or prefix:<name> (default all)
  --world-extent <units>   positions are replicated over [-extent, extent] (default 1000)
  --stop-file <path>       stop cleanly when this file exists
  --port-file <path>       write the bound UDP port here once the socket is open
  --status-file <path>     rewrite the status JSON here about once a second
  --json                   print the final status as one JSON object on stdout; log lines go to stderr
  --quiet                  no log lines
  --help                   this text

The server stops cleanly on SIGINT/SIGTERM (Ctrl+C or Ctrl+Break on Windows), on the stop file, and on the
time limits above. Exit codes: 0 stopped cleanly, 1 the server could not start or failed, 2 bad command line.
)";

struct ParsedArguments {
    kb::net::server::ServerOptions options;
    bool json = false;
    bool help = false;
    std::string error;
};

[[nodiscard]] bool ParseUnsigned(const std::string& text, unsigned long long limit, unsigned long long& value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 12U) {
        return false;
    }
    value = std::stoull(text);
    return value <= limit;
}

ParsedArguments Parse(const std::vector<std::string>& arguments) {
    ParsedArguments parsed;
    const std::set<std::string> flags{ "--json", "--quiet", "--help" };
    std::map<std::string, std::string> values;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (flags.contains(argument)) {
            values[argument] = "1";
        } else if (argument.rfind("--", 0U) == 0U) {
            if (index + 1U >= arguments.size()) {
                parsed.error = "the option " + argument + " needs a value";
                return parsed;
            }
            values[argument] = arguments[++index];
        } else {
            parsed.error = "unexpected argument: " + argument;
            return parsed;
        }
    }
    static const std::set<std::string> known{ "--project", "--keys", "--scene", "--port", "--bind", "--max-clients", "--tick-rate", "--idle-timeout",
        "--empty-timeout", "--max-seconds", "--replicate", "--world-extent", "--stop-file", "--port-file", "--status-file", "--json", "--quiet", "--help" };
    for (const auto& [name, value] : values) {
        if (!known.contains(name)) {
            parsed.error = "unknown option: " + name;
            return parsed;
        }
    }
    kb::net::server::ServerOptions& options = parsed.options;
    parsed.help = values.contains("--help");
    parsed.json = values.contains("--json");
    options.quiet = values.contains("--quiet");
    if (parsed.help) {
        return parsed;
    }
    const auto number = [&](const char* name, unsigned long long limit, auto& target) {
        const auto found = values.find(name);
        if (found == values.end()) {
            return true;
        }
        unsigned long long value = 0U;
        if (!ParseUnsigned(found->second, limit, value)) {
            parsed.error = std::string("the value of ") + name + " must be a whole number up to " + std::to_string(limit);
            return false;
        }
        target = static_cast<std::remove_reference_t<decltype(target)>>(value);
        return true;
    };
    if (!values.contains("--project") || !values.contains("--keys")) {
        parsed.error = "--project and --keys are required";
        return parsed;
    }
    options.project = values["--project"];
    options.keysFile = values["--keys"];
    if (values.contains("--scene")) options.scene = values["--scene"];
    if (values.contains("--bind")) options.bindAddress = values["--bind"];
    if (values.contains("--stop-file")) options.stopFile = values["--stop-file"];
    if (values.contains("--port-file")) options.portFile = values["--port-file"];
    if (values.contains("--status-file")) options.statusFile = values["--status-file"];
    if (!number("--port", 65535U, options.port) || !number("--max-clients", 4096U, options.maxClients) || !number("--tick-rate", 240U, options.tickRate) ||
        !number("--idle-timeout", 3600U, options.idleTimeoutSeconds) || !number("--empty-timeout", 86400U, options.emptyTimeoutSeconds) ||
        !number("--max-seconds", 31536000U, options.maxSeconds)) {
        return parsed;
    }
    if (const auto extent = values.find("--world-extent"); extent != values.end()) {
        try {
            std::size_t consumed = 0U;
            options.worldExtent = std::stof(extent->second, &consumed);
            if (consumed != extent->second.size()) {
                throw std::invalid_argument("extent");
            }
        } catch (...) {
            parsed.error = "the value of --world-extent must be a number";
            return parsed;
        }
    }
    if (const auto mode = values.find("--replicate"); mode != values.end()) {
        if (mode->second == "all") {
            options.replicate = kb::net::server::ReplicateMode::All;
        } else if (mode->second == "none") {
            options.replicate = kb::net::server::ReplicateMode::None;
        } else if (mode->second.rfind("prefix:", 0U) == 0U && mode->second.size() > 7U) {
            options.replicate = kb::net::server::ReplicateMode::Prefix;
            options.replicatePrefix = mode->second.substr(7U);
        } else {
            parsed.error = "--replicate expects all, none or prefix:<name>";
            return parsed;
        }
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const ParsedArguments parsed = Parse(arguments);
    if (parsed.help) {
        std::cout << kUsage << std::flush;
        return 0;
    }
    if (!parsed.error.empty()) {
        std::cerr << "error: " << parsed.error << "\nrun with --help for the options\n";
        return 2;
    }

#if defined(_WIN32)
    SetConsoleCtrlHandler(&ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, &SignalHandler);
    std::signal(SIGTERM, &SignalHandler);
#endif

    try {
        // With --json the log goes to stderr so that stdout is one clean JSON object.
        std::ostream& log = parsed.json ? std::cerr : std::cout;
        kb::net::server::DedicatedServer server(parsed.options, log);
        std::string error;
        if (!server.Start(error)) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        const int code = server.Run(g_stop);
        if (parsed.json) {
            std::cout << kb::net::server::SummaryJson(parsed.options, server.Summary()) << std::endl;
        }
        return code;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
