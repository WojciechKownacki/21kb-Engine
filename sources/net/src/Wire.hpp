#pragma once

#include <cstdint>

// Constants shared by the handshake (Host.cpp) and the session packets (Session.cpp).
namespace kb::net::wire {

inline constexpr std::uint16_t kMagic = 0x4B4EU;
inline constexpr std::uint8_t kVersion = 1U;
inline constexpr std::uint8_t kTypeConnect = 0x01U;
inline constexpr std::uint8_t kTypeChallenge = 0x02U;
inline constexpr std::uint8_t kTypeResponse = 0x03U;
inline constexpr std::uint8_t kTypeData = 0x10U;

} // namespace kb::net::wire
