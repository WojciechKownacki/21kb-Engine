#pragma once

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputRebinding.hpp"

namespace kb::input {
class InputSubsystem;

enum class InputRebindSessionState { Idle, Listening, Conflict, Applied, Canceled, Failed };

// One settings-screen binding capture. The caller polls physical UI input each
// frame; controls already held at Begin do not become accidental bindings.
class InputRebindSession {
public:
    bool Initialize(InputSubsystem& input, std::uint64_t contextId,
        const InputMappingContextAsset& defaults, std::filesystem::path profilePath);
    bool Begin(std::uint64_t bindingId, const InputDeviceState& device);
    InputRebindSessionState Poll(InputSubsystem& input, const InputDeviceState& device, bool swapConflict = false);
    void Cancel() noexcept;
    bool Reset(InputSubsystem& input);
    [[nodiscard]] InputRebindSessionState State() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t ConflictBinding() const noexcept { return conflict_; }
    [[nodiscard]] const std::string& Error() const noexcept { return error_; }

private:
    bool Store(InputSubsystem& input, std::span<const InputRebindOverride> profile);
    InputMappingContextAsset defaults_;
    InputDeviceState previous_;
    std::filesystem::path path_;
    std::uint64_t contextId_ = 0, bindingId_ = 0, conflict_ = 0;
    InputKey candidate_ = InputKey::None;
    std::uint8_t gamepad_ = 0;
    InputRebindSessionState state_ = InputRebindSessionState::Idle;
    std::string error_;
};
} // namespace kb::input
