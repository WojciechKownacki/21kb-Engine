#include "engine/input/InputRebindSession.hpp"
#include "engine/input/InputSubsystem.hpp"

#include <algorithm>
#include <cmath>

namespace kb::input {
bool InputRebindSession::Initialize(InputSubsystem& input, std::uint64_t contextId,
    const InputMappingContextAsset& defaults, std::filesystem::path profilePath) {
    Cancel();
    contextId_ = 0;
    error_.clear();
    std::error_code ec;
    const bool exists = std::filesystem::exists(profilePath, ec);
    if (ec || profilePath.empty()) { error_ = "invalid profile path"; return false; }
    std::vector<InputRebindOverride> profile;
    if (exists) {
        const auto loaded = ReadRebindProfile(profilePath);
        if (!loaded.succeeded) { error_ = "profile could not be loaded"; return false; }
        profile = loaded.asset;
    }
    if (!input.SetRebindProfile(contextId, profile)) { error_ = "context could not be resolved"; return false; }
    defaults_ = defaults;
    path_ = std::move(profilePath);
    contextId_ = contextId;
    state_ = InputRebindSessionState::Idle;
    return true;
}

bool InputRebindSession::Begin(std::uint64_t bindingId, const InputDeviceState& device) {
    if (contextId_ == 0 || bindingId == 0 || std::ranges::none_of(defaults_.mappings,
        [bindingId](const auto& mapping) { return mapping.bindingId == bindingId; })) return false;
    bindingId_ = bindingId;
    previous_ = device;
    candidate_ = InputKey::None;
    conflict_ = 0;
    error_.clear();
    state_ = InputRebindSessionState::Listening;
    return true;
}

void InputRebindSession::Cancel() noexcept {
    candidate_ = InputKey::None;
    conflict_ = 0;
    state_ = InputRebindSessionState::Canceled;
}

bool InputRebindSession::Store(InputSubsystem& input, std::span<const InputRebindOverride> profile) {
    const auto current = input.RebindProfile(contextId_);
    const std::vector<InputRebindOverride> before{current.begin(), current.end()};
    if (!input.SetRebindProfile(contextId_, profile)) { error_ = "profile could not be applied"; return false; }
    if (!WriteRebindProfile(path_, profile)) {
        static_cast<void>(input.SetRebindProfile(contextId_, before));
        error_ = "profile could not be saved";
        return false;
    }
    return true;
}

bool InputRebindSession::Reset(InputSubsystem& input) {
    Cancel();
    error_.clear();
    const bool reset = contextId_ != 0 && Store(input, {});
    state_ = reset ? InputRebindSessionState::Applied : InputRebindSessionState::Failed;
    return reset;
}

InputRebindSessionState InputRebindSession::Poll(InputSubsystem& input,
    const InputDeviceState& device, bool swapConflict) {
    if (state_ != InputRebindSessionState::Listening && state_ != InputRebindSessionState::Conflict) return state_;
    if (device.IsKeyDown(InputKey::Escape) && !previous_.IsKeyDown(InputKey::Escape)) {
        Cancel();
        return state_;
    }
    if (candidate_ == InputKey::None) {
        for (std::size_t index = 1; index < InputDeviceState::kKeyCount && candidate_ == InputKey::None; ++index) {
            const auto key = static_cast<InputKey>(index);
            if (ParseInputKey(ToString(key)) != key || key == InputKey::TouchDown ||
                (DeviceKindOf(key) == InputDeviceKind::Mouse && IsAnalogKey(key))) continue;
            const auto slots = DeviceKindOf(key) == InputDeviceKind::Gamepad ? InputDeviceState::kMaxGamepads : 1U;
            for (std::uint8_t pad = 0; pad < slots; ++pad) {
                if (std::abs(device.GetValue(key, pad)) > 0.5F && std::abs(previous_.GetValue(key, pad)) <= 0.5F) {
                    candidate_ = key;
                    gamepad_ = pad;
                    break;
                }
            }
        }
    }
    previous_ = device;
    if (candidate_ == InputKey::None) return state_;
    auto effective = defaults_;
    ApplyRebindProfile(effective, input.RebindProfile(contextId_));
    const auto target = std::ranges::find(effective.mappings, bindingId_, &InputKeyMapping::bindingId);
    const auto conflict = FindRebindConflict(effective, bindingId_, candidate_, gamepad_);
    conflict_ = conflict ? conflict->conflictingBindingId : 0;
    if (conflict && !swapConflict) return state_ = InputRebindSessionState::Conflict;
    if (conflict) {
        const auto other = std::ranges::find(effective.mappings, conflict_, &InputKeyMapping::bindingId);
        if (other == effective.mappings.end()) {
            error_ = "composite conflict requires another binding";
            return state_ = InputRebindSessionState::Conflict;
        }
        other->key = target->key;
        other->gamepadIndex = target->gamepadIndex;
    }
    target->key = candidate_;
    target->gamepadIndex = gamepad_;
    if (FindRebindConflict(effective, bindingId_, candidate_, gamepad_)) return state_ = InputRebindSessionState::Conflict;
    if (conflict) {
        const auto other = std::ranges::find(effective.mappings, conflict_, &InputKeyMapping::bindingId);
        if (FindRebindConflict(effective, conflict_, other->key, other->gamepadIndex)) return state_ = InputRebindSessionState::Conflict;
    }
    const auto current = input.RebindProfile(contextId_);
    std::vector<InputRebindOverride> profile{current.begin(), current.end()};
    for (std::size_t i = 0; i < effective.mappings.size(); ++i) {
        const auto& mapping = effective.mappings[i];
        const auto existing = std::ranges::find(profile, mapping.bindingId, &InputRebindOverride::bindingId);
        if (mapping.key == defaults_.mappings[i].key && mapping.gamepadIndex == defaults_.mappings[i].gamepadIndex) {
            if (existing != profile.end()) profile.erase(existing);
        } else if (existing != profile.end()) {
            *existing = {mapping.bindingId, mapping.key, mapping.gamepadIndex};
        } else {
            profile.push_back({mapping.bindingId, mapping.key, mapping.gamepadIndex});
        }
    }
    state_ = Store(input, profile) ? InputRebindSessionState::Applied : InputRebindSessionState::Failed;
    return state_;
}
} // namespace kb::input
