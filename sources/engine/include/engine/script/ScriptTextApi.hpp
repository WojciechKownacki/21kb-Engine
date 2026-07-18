#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-063: the script-facing surface for the locale-invariant parsing
// helpers in kb::library (EngineLibraryParsing). Register() wires Text.*
// functions (ParseInt/ParseUInt/ParseFloat/IsGuid/ParseColor/ParseDate)
// into the one ScriptFunctionRegistry, so a Native, Lua, or Visual Graph
// script can turn a string (config value, save payload, user input) into a
// typed value without depending on the process locale — each function
// forwards to the matching kb::library::TryParse* (std::from_chars-backed,
// culture-invariant) and reports success through a `ok` Bool output rather
// than throwing, mirroring the TryParse* contract itself.
class ScriptTextApi final {
public:
    ScriptTextApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
