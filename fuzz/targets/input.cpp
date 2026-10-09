// Input actions, mapping contexts, player rebinding profiles and input recordings.
#include "FuzzSupport.hpp"

#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputRebinding.hpp"
#include "engine/input/InputRecording.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes = kb::fuzz::Bytes(data, size);
    static_cast<void>(kb::input::DecodeInputAction(bytes));
    static_cast<void>(kb::input::DecodeInputMappingContext(bytes));
    static_cast<void>(kb::input::DecodeRebindProfile(bytes));
    static_cast<void>(kb::input::DecodeInputRecording(bytes));
    return 0;
}
