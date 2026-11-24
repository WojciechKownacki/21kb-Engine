#include "kb/net/LanDiscovery.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#elif !defined(__EMSCRIPTEN__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

// Platform queries the network layer needs beyond the sockets themselves. Kept apart from the
// transports so every platform answers in one place.
namespace kb::net {

namespace {

void AddUnique(std::vector<Endpoint>& endpoints, Endpoint endpoint) {
    if (endpoint.address != 0U && std::find(endpoints.begin(), endpoints.end(), endpoint) == endpoints.end()) {
        endpoints.push_back(endpoint);
    }
}

} // namespace

std::vector<Endpoint> ListBroadcastEndpoints(std::uint16_t port) {
    std::vector<Endpoint> endpoints;
    // The limited broadcast always leaves through one interface; on a multi-homed machine the
    // directed broadcast of each interface reaches the rest of the link.
    AddUnique(endpoints, Endpoint::Make(255U, 255U, 255U, 255U, port));
#if defined(_WIN32)
    ULONG bytes = 0U;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &bytes) !=
            ERROR_BUFFER_OVERFLOW || bytes == 0U) {
        return endpoints;
    }
    std::vector<std::uint8_t> storage(bytes);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, addresses, &bytes) != NO_ERROR) {
        return endpoints;
    }
    for (const IP_ADAPTER_ADDRESSES* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            const std::uint32_t host = ntohl(address->sin_addr.s_addr);
            if ((host >> 24U) == 127U) {
                continue;
            }
            const ULONG prefix = unicast->OnLinkPrefixLength;
            if (prefix == 0U || prefix >= 32U) {
                continue;
            }
            const std::uint32_t mask = 0xFFFFFFFFU << (32U - prefix);
            AddUnique(endpoints, Endpoint{ host | ~mask, port });
        }
    }
#elif defined(__EMSCRIPTEN__)
    // No interface list in the browser build.
#elif defined(__ANDROID__) && defined(__ANDROID_API__) && (__ANDROID_API__ < 24)
    // getifaddrs arrived in Android 8 (API 24); older devices get only the limited broadcast.
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return endpoints;
    }
    for (const ifaddrs* entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || (entry->ifa_flags & IFF_BROADCAST) == 0 ||
            entry->ifa_broadaddr == nullptr) {
            continue;
        }
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_broadaddr);
        AddUnique(endpoints, Endpoint{ ntohl(address->sin_addr.s_addr), port });
    }
    freeifaddrs(interfaces);
#endif
    return endpoints;
}

} // namespace kb::net
