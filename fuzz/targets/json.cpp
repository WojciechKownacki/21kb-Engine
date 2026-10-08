// The JSON reader behind the CLI, the editor automation and the signing broker.
#include "FuzzSupport.hpp"

#include "engine/core/JsonValue.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    kb::core::JsonValue value;
    std::string error;
    if (kb::core::JsonValue::Parse(kb::fuzz::Text(data, size), value, error)) {
        static_cast<void>(value.Dump());
    }
    return 0;
}
