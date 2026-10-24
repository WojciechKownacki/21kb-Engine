#include "GameScriptLog.hpp"

#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace kb::game {
namespace {

bool WriteScriptLog(std::string_view message) {
    constexpr std::string_view prefix = "kb_game: script: ";
#if defined(__ANDROID__)
    const std::string line = std::string{prefix} + std::string{message};
    return __android_log_write(ANDROID_LOG_INFO, "21kb", line.c_str()) >= 0;
#else
    return std::fwrite(prefix.data(), 1U, prefix.size(), stdout) == prefix.size() &&
        std::fwrite(message.data(), 1U, message.size(), stdout) == message.size() &&
        std::fputc('\n', stdout) != EOF && std::fflush(stdout) == 0;
#endif
}

} // namespace

void ConfigureGameScriptLog(kb::script::ScriptRuntimeHost& host) {
    kb::script::ScriptFunctionDesc function;
    function.signature.name = "Log";
    function.signature.inputs = {{"message", kb::script::ScriptValueType::String, true}};
    function.callback = [](const kb::script::ScriptFunctionCallContext&,
                           std::span<const kb::script::ScriptFunctionArgument> arguments) {
        const bool written = WriteScriptLog(arguments.front().value.AsString());
        return kb::script::ScriptFunctionCallResult{
            .executed = written,
            .outputs = {},
            .errors = written ? std::vector<std::string>{}
                : std::vector<std::string>{"game script log output failed"},
        };
    };
    if (!host.RegisterFunction(std::move(function))) {
        throw std::runtime_error("game script log function could not be registered");
    }
}

} // namespace kb::game
