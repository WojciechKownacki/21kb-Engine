#pragma once

#include "engine/script/NativeScriptPlugin.hpp"

#include <string_view>

namespace kb::script {

// Project C++ that a target cannot load as a library (Android, WebAssembly) is linked into
// the host. Each module announces its registration entry here before main() runs.
class NativeLinkedModules final {
public:
    static void Add(std::string_view key, NativeScriptPluginRegisterProc registration) noexcept;
    [[nodiscard]] static NativeScriptPluginRegisterProc Find(std::string_view key) noexcept;
};

} // namespace kb::script
