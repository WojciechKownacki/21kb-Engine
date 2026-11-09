#include "engine/script/NativeLinkedModules.hpp"

#include <map>
#include <string>

namespace kb::script {
namespace {

std::map<std::string, NativeScriptPluginRegisterProc, std::less<>>& Modules() {
    static std::map<std::string, NativeScriptPluginRegisterProc, std::less<>> modules;
    return modules;
}

} // namespace

void NativeLinkedModules::Add(std::string_view key, NativeScriptPluginRegisterProc registration) noexcept {
    try {
        Modules()[std::string{ key }] = registration;
    } catch (...) {
        // A module that cannot be recorded stays unregistered; the runtime reports it as missing.
    }
}

NativeScriptPluginRegisterProc NativeLinkedModules::Find(std::string_view key) noexcept {
    const auto found = Modules().find(key);
    return found == Modules().end() ? nullptr : found->second;
}

} // namespace kb::script
