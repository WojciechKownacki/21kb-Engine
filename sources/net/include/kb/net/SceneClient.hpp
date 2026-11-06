#pragma once

#include "kb/net/Host.hpp"
#include <functional>
#include <iosfwd>

namespace kb::scene { class Scene; }
namespace kb::net {

struct SceneClientOptions {
    std::uint64_t clientId = 0U;
    SessionKey key{};
    Endpoint server{};
    std::string token;
    float worldExtent = 1000.0F;
    std::uint32_t reconnectAttempts = 3U;
};

// Runtime-only Web settings. IDs are decimal strings to preserve all 64 bits.
[[nodiscard]] bool ParseWebSceneClientSettings(std::string_view json,
    SceneClientOptions& options, std::string& url);

// Runtime-only UDP settings: literal IPv4 address, numeric port and the same
// string identity/session credentials as the browser client. Failure leaves options unchanged.
[[nodiscard]] bool ParseNativeSceneClientSettings(std::string_view json, SceneClientOptions& options);

// Attach before the first script update. Update after scripts have registered
// their RPCs; subsequent updates pump replication and bounded reconnects.
class SceneClient final {
public:
    using TransportFactory = std::function<std::unique_ptr<IDatagramTransport>(std::string&)>;
    SceneClient(kb::scene::Scene& scene, SceneClientOptions options, TransportFactory factory);
    ~SceneClient();
    SceneClient(const SceneClient&) = delete;
    SceneClient& operator=(const SceneClient&) = delete;
    [[nodiscard]] bool Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kb::net
