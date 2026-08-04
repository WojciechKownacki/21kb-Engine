#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kb::editor {

class EditorHostSurfaceLifecycle {
public:
    using HostKey = std::uintptr_t;

    void TrackHost(HostKey host) {
        if (host == 0U || Find(host) != nullptr) return;
        hosts_.push_back(HostState{ .host = host, .suspended = false });
    }

    [[nodiscard]] bool Suspend(HostKey host) noexcept {
        HostState* state = Find(host);
        if (state == nullptr || state->suspended) return false;
        state->suspended = true;
        return true;
    }

    [[nodiscard]] bool Resume(HostKey host) noexcept {
        HostState* state = Find(host);
        if (allSuspended_ || state == nullptr || !state->suspended) return false;
        state->suspended = false;
        return true;
    }

    [[nodiscard]] bool SuspendAll() noexcept {
        if (allSuspended_) return false;
        allSuspended_ = true;
        return true;
    }

    [[nodiscard]] bool ResumeAll() noexcept {
        if (!allSuspended_) return false;
        allSuspended_ = false;
        return true;
    }

    [[nodiscard]] bool IsSuspended(HostKey host) const noexcept {
        const HostState* state = Find(host);
        return allSuspended_ || (state != nullptr && state->suspended);
    }

    void Clear() noexcept {
        hosts_.clear();
        allSuspended_ = false;
    }

private:
    struct HostState {
        HostKey host = 0U;
        bool suspended = false;
    };

    [[nodiscard]] HostState* Find(HostKey host) noexcept {
        const auto iter = std::find_if(hosts_.begin(), hosts_.end(), [host](const HostState& state) {
            return state.host == host;
        });
        return iter == hosts_.end() ? nullptr : &*iter;
    }

    [[nodiscard]] const HostState* Find(HostKey host) const noexcept {
        const auto iter = std::find_if(hosts_.begin(), hosts_.end(), [host](const HostState& state) {
            return state.host == host;
        });
        return iter == hosts_.end() ? nullptr : &*iter;
    }

    std::vector<HostState> hosts_;
    bool allSuspended_ = false;
};

} // namespace kb::editor
