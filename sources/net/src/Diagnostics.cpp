#include "kb/net/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace kb::net {

std::string_view DiagName(DiagCode code) noexcept {
    switch (code) {
    case DiagCode::EntityLimit: return "EntityLimit";
    case DiagCode::OversizeRecord: return "OversizeRecord";
    case DiagCode::SendQueueFull: return "SendQueueFull";
    case DiagCode::HandshakeVersion: return "HandshakeVersion";
    case DiagCode::HandshakeContent: return "HandshakeContent";
    case DiagCode::HandshakeRateLimited: return "HandshakeRateLimited";
    case DiagCode::HandshakeServerFull: return "HandshakeServerFull";
    case DiagCode::RpcRejected: return "RpcRejected";
    case DiagCode::InterestBudget: return "InterestBudget";
    case DiagCode::ObjectCountWarning: return "ObjectCountWarning";
    case DiagCode::VersionUnchecked: return "VersionUnchecked";
    case DiagCode::VisibilityUnbounded: return "VisibilityUnbounded";
    case DiagCode::RateLimited: return "RateLimited";
    case DiagCode::ComponentGroupLimit: return "ComponentGroupLimit";
    case DiagCode::NotAuthority: return "NotAuthority";
    case DiagCode::EventsDropped: return "EventsDropped";
    case DiagCode::ConnectBucketsFull: return "ConnectBucketsFull";
    case DiagCode::PasswordRejected: return "PasswordRejected";
    case DiagCode::PasswordRateLimited: return "PasswordRateLimited";
    case DiagCode::UnauthenticatedGuests: return "UnauthenticatedGuests";
    }
    return "Unknown";
}

void DiagLog::Report(DiagCode code, std::string_view detail) noexcept {
    ++total_;
    std::size_t index = 0U;
    while (index < size_ && entries_[index].code != code) ++index;
    if (index == size_) {
        if (size_ == entries_.size()) return;
        entries_[size_++].code = code;
    }
    DiagEntry& entry = entries_[index];
    ++entry.count;
    entry.lastMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const std::size_t length = std::min(detail.size(), sizeof(entry.detail) - 1U);
    if (length != 0U) std::memcpy(entry.detail, detail.data(), length);
    entry.detail[length] = '\0';
    if (sink_ && (!emitted_[index] || entry.lastMs - emittedAt_[index] >= 60'000U)) {
        emitted_[index] = true;
        emittedAt_[index] = entry.lastMs;
        char line[224]{};
        const std::string_view name = DiagName(code);
        std::snprintf(line, sizeof(line), "%.*s: %s", static_cast<int>(name.size()), name.data(), entry.detail);
        try { sink_(line); } catch (...) { }
    }
}

} // namespace kb::net
