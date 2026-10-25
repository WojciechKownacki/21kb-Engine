#include "kb/net/Host.hpp"

#include "Wire.hpp"
#include "kb/net/ByteIO.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace kb::net {

namespace {

constexpr std::size_t kHandshakeHeaderBytes = 4U;
constexpr std::size_t kMacBytes = 32U;
constexpr std::size_t kChallengeBytes = kHandshakeHeaderBytes + 8U + 8U + kMacBytes;
constexpr std::size_t kResponseBytes = kHandshakeHeaderBytes + 8U + 8U + 8U + kMacBytes;
static_assert(kChallengeBytes < kMinConnectBytes, "a Challenge must be smaller than the Connect that asks for it");

constexpr std::size_t kMaxQueuedEvents = 4096U;
constexpr std::size_t kClientReceiveLimit = 256U;
constexpr std::size_t kClientPacketsPerUpdate = 16U;

using Mac = std::array<std::uint8_t, kMacBytes>;

[[nodiscard]] std::span<const std::uint8_t> AsBytes(std::string_view text) noexcept {
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

[[nodiscard]] Mac ComputeMac(const SessionKey& key, std::string_view label, std::uint64_t clientId, std::uint64_t clientSalt,
    std::uint64_t serverSalt) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.Bytes(AsBytes(label));
    writer.U64(clientId);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    const kb::security::Sha512Digest digest = kb::security::HmacSha512(key, message);
    Mac mac{};
    std::copy_n(digest.begin(), mac.size(), mac.begin());
    return mac;
}

void DeriveSessionKeys(const SessionKey& key, std::uint64_t clientSalt, std::uint64_t serverSalt, SessionKey& clientToServer,
    SessionKey& serverToClient) {
    std::vector<std::uint8_t> salt;
    ByteWriter writer(salt);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    std::array<std::uint8_t, 2U * kSessionKeyBytes> output{};
    kb::security::HkdfSha512(output, key, salt, AsBytes("21kb-net-session-v1"));
    std::copy_n(output.begin(), kSessionKeyBytes, clientToServer.begin());
    std::copy_n(output.begin() + static_cast<std::ptrdiff_t>(kSessionKeyBytes), kSessionKeyBytes, serverToClient.begin());
    kb::security::SecureWipe(output);
}

[[nodiscard]] bool ReadHandshakeHeader(ByteReader& reader, std::uint8_t& type) noexcept {
    const std::uint16_t magic = reader.U16();
    const std::uint8_t version = reader.U8();
    type = reader.U8();
    return reader.Ok() && magic == wire::kMagic && version == wire::kVersion;
}

void WriteHandshakeHeader(ByteWriter& writer, std::uint8_t type) {
    writer.U16(wire::kMagic);
    writer.U8(wire::kVersion);
    writer.U8(type);
}

} // namespace

// ---------------------------------------------------------------- Host

Host::Host(IDatagramTransport& transport, HostConfig config, const IKeyProvider& keys)
    : transport_(&transport), keys_(&keys), config_(std::move(config)) {
    valid_ = config_.session.IsValid() && config_.maxClients != 0U && config_.maxDatagramsPerUpdate != 0U &&
        config_.maxPacketsPerPeerPerUpdate != 0U && config_.cookieLifetimeMilliseconds != 0U &&
        kb::security::SecureRandom(cookieSecret_);
}

std::uint64_t Host::Cookie(const Endpoint& from, std::uint64_t clientId, std::uint64_t clientSalt, std::uint64_t epoch) const {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U32(from.address);
    writer.U16(from.port);
    writer.U64(clientId);
    writer.U64(clientSalt);
    writer.U64(epoch);
    const kb::security::Sha512Digest digest = kb::security::HmacSha512(cookieSecret_, message);
    ByteReader reader(std::span<const std::uint8_t>(digest.data(), 8U));
    return reader.U64();
}

void Host::Update(std::uint64_t nowMilliseconds) {
    if (!valid_) {
        return;
    }
    for (auto& entry : peers_) {
        entry.second.packetsThisUpdate = 0U;
    }

    std::array<std::uint8_t, kMaxDatagramBytes + 1U> buffer{};
    for (std::size_t received = 0U; received < config_.maxDatagramsPerUpdate; ++received) {
        Endpoint from;
        std::size_t size = 0U;
        const TransportStatus status = transport_->Receive(from, buffer, size);
        if (status == TransportStatus::WouldBlock) {
            break;
        }
        if (status != TransportStatus::Ok) {
            ++stats_.sendErrors;
            break;
        }
        ++stats_.datagramsReceived;
        if (size > kMaxDatagramBytes) {
            ++stats_.oversized;
            continue;
        }
        const std::span<const std::uint8_t> datagram(buffer.data(), size);
        ByteReader reader(datagram);
        std::uint8_t type = 0U;
        if (!ReadHandshakeHeader(reader, type)) {
            ++stats_.malformed;
            continue;
        }
        switch (type) {
        case wire::kTypeConnect: HandleConnect(nowMilliseconds, from, datagram); break;
        case wire::kTypeResponse: HandleResponse(nowMilliseconds, from, datagram); break;
        case wire::kTypeData: HandleData(nowMilliseconds, from, datagram); break;
        default: ++stats_.malformed; break;
        }
    }

    for (auto iterator = peers_.begin(); iterator != peers_.end();) {
        Peer& peer = iterator->second;
        DrainMessages(peer);
        const auto current = iterator++;
        if (peer.session->IsClosed()) {
            RemovePeer(current, peer.session->PeerDisconnected() ? DisconnectReason::PeerClosed : DisconnectReason::Requested);
        } else if (peer.session->TimedOut(nowMilliseconds)) {
            RemovePeer(current, DisconnectReason::Timeout);
        }
    }

    for (auto& entry : peers_) {
        Peer& peer = entry.second;
        for (std::size_t sent = 0U; sent < config_.maxPacketsPerPeerPerUpdate; ++sent) {
            const std::optional<std::vector<std::uint8_t>> packet = peer.session->BuildPacket(nowMilliseconds);
            if (!packet.has_value()) {
                break;
            }
            if (transport_->Send(peer.endpoint, *packet) == TransportStatus::Error) {
                ++stats_.sendErrors;
            }
        }
    }
}

void Host::HandleConnect(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    if (datagram.size() < kMinConnectBytes) {
        ++stats_.handshakesRejected;
        return;
    }
    ByteReader reader(datagram.subspan(kHandshakeHeaderBytes));
    const std::uint64_t clientId = reader.U64();
    const std::uint64_t clientSalt = reader.U64();
    SessionKey key{};
    if (!reader.Ok() || clientId == 0U || !keys_->LookupKey(clientId, key)) {
        ++stats_.handshakesRejected;
        return;
    }
    const std::uint64_t serverSalt = Cookie(from, clientId, clientSalt, nowMilliseconds / config_.cookieLifetimeMilliseconds);
    const Mac mac = ComputeMac(key, "21kb-net-challenge", clientId, clientSalt, serverSalt);
    std::vector<std::uint8_t> challenge;
    ByteWriter writer(challenge);
    WriteHandshakeHeader(writer, wire::kTypeChallenge);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    writer.Bytes(mac);
    if (transport_->Send(from, challenge) == TransportStatus::Error) {
        ++stats_.sendErrors;
    }
    ++stats_.challengesSent;
    kb::security::SecureWipe(key);
}

void Host::HandleResponse(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    if (datagram.size() != kResponseBytes) {
        ++stats_.handshakesRejected;
        return;
    }
    ByteReader reader(datagram.subspan(kHandshakeHeaderBytes));
    const std::uint64_t clientId = reader.U64();
    const std::uint64_t clientSalt = reader.U64();
    const std::uint64_t serverSalt = reader.U64();
    const std::span<const std::uint8_t> receivedMac = reader.Bytes(kMacBytes);
    SessionKey key{};
    if (!reader.Ok() || clientId == 0U || !keys_->LookupKey(clientId, key)) {
        ++stats_.handshakesRejected;
        return;
    }

    // The cookie proves this address received the Challenge; the MAC proves the sender holds the key.
    const std::uint64_t epoch = nowMilliseconds / config_.cookieLifetimeMilliseconds;
    const bool cookieOk = serverSalt == Cookie(from, clientId, clientSalt, epoch) ||
        (epoch != 0U && serverSalt == Cookie(from, clientId, clientSalt, epoch - 1U));
    const Mac expected = ComputeMac(key, "21kb-net-response", clientId, clientSalt, serverSalt);
    if (!cookieOk || !kb::security::ConstantTimeEqual(expected, receivedMac)) {
        ++stats_.handshakesRejected;
        kb::security::SecureWipe(key);
        return;
    }

    const auto existing = peers_.find(from);
    if (existing != peers_.end()) {
        if (existing->second.clientId == clientId && existing->second.clientSalt == clientSalt && existing->second.serverSalt == serverSalt) {
            existing->second.session->Poke(); // the Response was repeated: our confirmation was probably lost
            kb::security::SecureWipe(key);
            return;
        }
        RemovePeer(existing, DisconnectReason::Replaced);
    }
    const auto sameClient = byClient_.find(clientId);
    if (sameClient != byClient_.end()) {
        const auto old = peers_.find(sameClient->second);
        if (old != peers_.end()) {
            RemovePeer(old, DisconnectReason::Replaced);
        }
    }
    if (peers_.size() >= config_.maxClients) {
        ++stats_.handshakesRejected;
        kb::security::SecureWipe(key);
        return;
    }

    SessionKey clientToServer{};
    SessionKey serverToClient{};
    DeriveSessionKeys(key, clientSalt, serverSalt, clientToServer, serverToClient);
    kb::security::SecureWipe(key);

    Peer peer;
    peer.endpoint = from;
    peer.clientId = clientId;
    peer.clientSalt = clientSalt;
    peer.serverSalt = serverSalt;
    peer.session = std::make_unique<Session>(config_.session, serverToClient, clientToServer, nowMilliseconds);
    kb::security::SecureWipe(clientToServer);
    kb::security::SecureWipe(serverToClient);
    peers_.emplace(from, std::move(peer));
    byClient_[clientId] = from;
    ++stats_.handshakesCompleted;
    NetEvent event;
    event.type = NetEventType::Connected;
    event.peer = clientId;
    events_.push_back(std::move(event));
}

void Host::HandleData(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    const auto found = peers_.find(from);
    if (found == peers_.end()) {
        ++stats_.unknownPeer;
        return;
    }
    Peer& peer = found->second;
    if (peer.packetsThisUpdate >= config_.session.limits.maxMessagesPerTick) {
        ++stats_.rateLimited;
        return;
    }
    ++peer.packetsThisUpdate;
    static_cast<void>(peer.session->ProcessPacket(nowMilliseconds, datagram));
    DrainMessages(peer);
}

void Host::DrainMessages(Peer& peer) {
    Message message;
    while (events_.size() < kMaxQueuedEvents && peer.session->PopMessage(message)) {
        NetEvent event;
        event.type = NetEventType::Message;
        event.peer = peer.clientId;
        event.message = std::move(message);
        events_.push_back(std::move(event));
    }
}

void Host::RemovePeer(std::map<Endpoint, Peer>::iterator found, DisconnectReason reason) {
    // Messages that already arrived are still delivered before the disconnect event.
    DrainMessages(found->second);
    const auto byClient = byClient_.find(found->second.clientId);
    if (byClient != byClient_.end() && byClient->second == found->first) {
        byClient_.erase(byClient);
    }
    NetEvent event;
    event.type = NetEventType::Disconnected;
    event.peer = found->second.clientId;
    event.reason = reason;
    events_.push_back(std::move(event));
    peers_.erase(found);
}

SendResult Host::Send(std::uint64_t clientId, std::uint8_t channel, std::span<const std::uint8_t> data) {
    const auto byClient = byClient_.find(clientId);
    if (byClient == byClient_.end()) {
        return SendResult::Closed;
    }
    return peers_.at(byClient->second).session->Send(channel, data);
}

void Host::Disconnect(std::uint64_t clientId) {
    const auto byClient = byClient_.find(clientId);
    if (byClient != byClient_.end()) {
        peers_.at(byClient->second).session->Disconnect();
    }
}

bool Host::PollEvent(NetEvent& event) {
    if (events_.empty()) {
        return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

const SessionStats* Host::ClientStats(std::uint64_t clientId) const noexcept {
    const auto byClient = byClient_.find(clientId);
    if (byClient == byClient_.end()) {
        return nullptr;
    }
    const auto peer = peers_.find(byClient->second);
    return peer == peers_.end() ? nullptr : &peer->second.session->Stats();
}

// -------------------------------------------------------------- Client

Client::Client(IDatagramTransport& transport, ClientConfig config, std::uint64_t clientId, const SessionKey& key, Endpoint host,
    std::uint64_t nowMilliseconds)
    : transport_(&transport), config_(std::move(config)), clientId_(clientId), key_(key), host_(host), clientSalt_(config_.clientSalt),
      startedAt_(nowMilliseconds), lastHandshakeSend_(nowMilliseconds) {
    if (!config_.session.IsValid() || clientId_ == 0U || config_.retryMilliseconds == 0U) {
        Fail(DisconnectReason::Error);
        return;
    }
    if (clientSalt_ == 0U) {
        std::array<std::uint8_t, 8U> random{};
        if (!kb::security::SecureRandom(random)) {
            Fail(DisconnectReason::Error);
            return;
        }
        ByteReader reader(random);
        clientSalt_ = reader.U64();
    }
}

void Client::Fail(DisconnectReason reason) {
    if (state_ == ClientState::Disconnected) {
        return;
    }
    state_ = ClientState::Disconnected;
    NetEvent event;
    event.type = NetEventType::Disconnected;
    event.reason = reason;
    events_.push_back(std::move(event));
}

void Client::SendConnect() {
    std::vector<std::uint8_t> packet;
    ByteWriter writer(packet);
    WriteHandshakeHeader(writer, wire::kTypeConnect);
    writer.U64(clientId_);
    writer.U64(clientSalt_);
    writer.Zeros(kMinConnectBytes - packet.size());
    static_cast<void>(transport_->Send(host_, packet));
}

void Client::SendResponse() {
    const Mac mac = ComputeMac(key_, "21kb-net-response", clientId_, clientSalt_, serverSalt_);
    std::vector<std::uint8_t> packet;
    ByteWriter writer(packet);
    WriteHandshakeHeader(writer, wire::kTypeResponse);
    writer.U64(clientId_);
    writer.U64(clientSalt_);
    writer.U64(serverSalt_);
    writer.Bytes(mac);
    static_cast<void>(transport_->Send(host_, packet));
}

void Client::HandleDatagram(std::uint64_t nowMilliseconds, std::span<const std::uint8_t> datagram) {
    ByteReader reader(datagram);
    std::uint8_t type = 0U;
    if (!ReadHandshakeHeader(reader, type)) {
        return;
    }
    if (type == wire::kTypeChallenge) {
        if (stage_ != Stage::Connect || datagram.size() != kChallengeBytes) {
            return;
        }
        const std::uint64_t echoedSalt = reader.U64();
        const std::uint64_t serverSalt = reader.U64();
        const std::span<const std::uint8_t> receivedMac = reader.Bytes(kMacBytes);
        if (!reader.Ok() || echoedSalt != clientSalt_) {
            return;
        }
        const Mac expected = ComputeMac(key_, "21kb-net-challenge", clientId_, clientSalt_, serverSalt);
        if (!kb::security::ConstantTimeEqual(expected, receivedMac)) {
            return; // the host does not know the key: not the host we meant to reach
        }
        serverSalt_ = serverSalt;
        SessionKey clientToServer{};
        SessionKey serverToClient{};
        DeriveSessionKeys(key_, clientSalt_, serverSalt_, clientToServer, serverToClient);
        session_ = std::make_unique<Session>(config_.session, clientToServer, serverToClient, nowMilliseconds);
        kb::security::SecureWipe(clientToServer);
        kb::security::SecureWipe(serverToClient);
        stage_ = Stage::Response;
        SendResponse();
        lastHandshakeSend_ = nowMilliseconds;
        return;
    }
    if (type == wire::kTypeData && session_ != nullptr) {
        const PacketResult result = session_->ProcessPacket(nowMilliseconds, datagram);
        if (result == PacketResult::Accepted && stage_ == Stage::Response) {
            stage_ = Stage::Established;
            state_ = ClientState::Connected;
            NetEvent event;
            event.type = NetEventType::Connected;
            events_.push_back(std::move(event));
        }
    }
}

void Client::Update(std::uint64_t nowMilliseconds) {
    if (state_ == ClientState::Disconnected) {
        return;
    }
    std::array<std::uint8_t, kMaxDatagramBytes + 1U> buffer{};
    for (std::size_t received = 0U; received < kClientReceiveLimit; ++received) {
        Endpoint from;
        std::size_t size = 0U;
        if (transport_->Receive(from, buffer, size) != TransportStatus::Ok) {
            break;
        }
        if (from == host_ && size <= kMaxDatagramBytes) {
            HandleDatagram(nowMilliseconds, std::span<const std::uint8_t>(buffer.data(), size));
        }
    }

    if (state_ == ClientState::Connecting) {
        if (nowMilliseconds - startedAt_ >= config_.connectTimeoutMilliseconds) {
            Fail(DisconnectReason::ConnectTimeout);
            return;
        }
        if (!sentFirst_ || nowMilliseconds - lastHandshakeSend_ >= config_.retryMilliseconds) {
            if (stage_ == Stage::Connect) {
                SendConnect();
            } else {
                SendResponse();
            }
            sentFirst_ = true;
            lastHandshakeSend_ = nowMilliseconds;
        }
        return;
    }

    Message message;
    while (events_.size() < kMaxQueuedEvents && session_->PopMessage(message)) {
        NetEvent event;
        event.type = NetEventType::Message;
        event.message = std::move(message);
        events_.push_back(std::move(event));
    }
    if (session_->IsClosed()) {
        Fail(session_->PeerDisconnected() ? DisconnectReason::PeerClosed : DisconnectReason::Requested);
        return;
    }
    if (session_->TimedOut(nowMilliseconds)) {
        Fail(DisconnectReason::Timeout);
        return;
    }
    for (std::size_t sent = 0U; sent < kClientPacketsPerUpdate; ++sent) {
        const std::optional<std::vector<std::uint8_t>> packet = session_->BuildPacket(nowMilliseconds);
        if (!packet.has_value()) {
            break;
        }
        static_cast<void>(transport_->Send(host_, *packet));
    }
}

SendResult Client::Send(std::uint8_t channel, std::span<const std::uint8_t> data) {
    return state_ == ClientState::Connected ? session_->Send(channel, data) : SendResult::Closed;
}

void Client::Disconnect() {
    if (state_ == ClientState::Connected) {
        session_->Disconnect();
    } else {
        Fail(DisconnectReason::Requested);
    }
}

bool Client::PollEvent(NetEvent& event) {
    if (events_.empty()) {
        return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

} // namespace kb::net
