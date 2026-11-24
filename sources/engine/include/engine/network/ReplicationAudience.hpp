#pragma once

#include <cstdint>
#include <string_view>

namespace kb::network {

enum class ReplicationAudience : std::uint8_t { Everyone, Owner, Others, InitialOnly };

[[nodiscard]] constexpr std::string_view ReplicationAudienceName(ReplicationAudience audience) noexcept {
    switch (audience) {
    case ReplicationAudience::Everyone: return "Everyone";
    case ReplicationAudience::Owner: return "Owner";
    case ReplicationAudience::Others: return "Others";
    case ReplicationAudience::InitialOnly: return "InitialOnly";
    }
    return "Invalid";
}

} // namespace kb::network
