#include "kb/net/Host.hpp"

#include "Wire.hpp"
#include "kb/net/ByteIO.hpp"
#include "kb/net/Auth.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace kb::net {

namespace {

constexpr std::size_t kHandshakeHeaderBytes = 4U;
constexpr std::size_t kMacBytes = 32U;
constexpr std::size_t kChallengeBytes = kHandshakeHeaderBytes + 8U + 8U + kMacBytes;
constexpr std::size_t kAcceptPlainBytes = 44U;
constexpr std::size_t kAcceptPrefixBytes = kHandshakeHeaderBytes + kb::security::kAeadNonceBytes;
constexpr std::size_t kAcceptBytes = kAcceptPrefixBytes + kAcceptPlainBytes + kb::security::kAeadTagBytes;
constexpr std::size_t kRejectBytes = 25U;
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

void DeriveSessionKeys(const SessionKey& key, std::uint64_t clientSalt, std::uint64_t serverSalt, std::span<const std::uint8_t, 32U> serverRandom,
    SessionKey& clientToServer, SessionKey& serverToClient) {
    std::vector<std::uint8_t> salt;
    ByteWriter writer(salt);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    writer.Bytes(serverRandom);
    std::array<std::uint8_t, 2U * kSessionKeyBytes> output{};
    kb::security::HkdfSha512(output, key, salt, AsBytes("21kb-net-session-v2"));
    std::copy_n(output.begin(), kSessionKeyBytes, clientToServer.begin());
    std::copy_n(output.begin() + static_cast<std::ptrdiff_t>(kSessionKeyBytes), kSessionKeyBytes, serverToClient.begin());
    kb::security::SecureWipe(output);
}

void DeriveAcceptKey(const SessionKey& key, std::uint64_t clientSalt, std::uint64_t serverSalt, kb::security::AeadKey& out) {
    std::vector<std::uint8_t> salt;
    ByteWriter writer(salt);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    kb::security::HkdfSha512(out.Span(), key, salt, AsBytes("21kb-net-accept-v2"));
}

[[nodiscard]] bool ReadHeaderPrefix(ByteReader& reader, std::uint8_t& version, std::uint8_t& type) noexcept {
    const std::uint16_t magic = reader.U16();
    version = reader.U8();
    type = reader.U8();
    return reader.Ok() && magic == wire::kMagic;
}

void WriteHandshakeHeader(ByteWriter& writer, std::uint8_t type) {
    writer.U16(wire::kMagic);
    writer.U8(wire::kVersion);
    writer.U8(type);
}

} // namespace

BuildIdentity MakeBuildIdentity(std::uint32_t release, std::string_view contentVersion) noexcept {
    BuildIdentity identity{ release, 0U };
    if (!contentVersion.empty()) {
        const auto digest = kb::security::Sha512(AsBytes(contentVersion));
        ByteReader reader(std::span<const std::uint8_t>(digest.data(), 8U));
        identity.contentHash = reader.U64();
    }
    return identity;
}

// ---------------------------------------------------------------- Host

Host::Host(IDatagramTransport& transport, HostConfig config, const IKeyProvider& keys)
    : transport_(&transport), keys_(&keys), config_(std::move(config)) {
    valid_ = config_.session.IsValid() && config_.maxClients != 0U && config_.maxDatagramsPerUpdate != 0U &&
        config_.maxPacketsPerPeerPerUpdate != 0U && config_.cookieLifetimeMilliseconds != 0U && config_.confirmMilliseconds != 0U &&
        kb::security::SecureRandom(cookieSecret_);
    if (config_.identity.release == 0U && config_.identity.contentHash == 0U) {
        diagnostics_.Report(DiagCode::VersionUnchecked, "host build identity is unchecked");
    }
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
        std::uint8_t version = 0U;
        if (!ReadHeaderPrefix(reader, version, type)) {
            ++stats_.malformed;
            diagnostics_.Report(DiagCode::HandshakeVersion, "invalid handshake header or version");
            continue;
        }
        if (version != wire::kVersion) {
            ++stats_.handshakesRejected;
            diagnostics_.Report(DiagCode::HandshakeVersion, "unsupported protocol version");
            if (type == wire::kTypeConnect && datagram.size() >= kMinConnectBytes) {
                static_cast<void>(reader.U64());
                SendReject(from, RejectReason::ProtocolVersion, reader.U64());
            }
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
        } else if ((!peer.confirmed && nowMilliseconds - peer.createdAt >= config_.confirmMilliseconds) ||
            peer.session->TimedOut(nowMilliseconds)) {
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

void Host::SendReject(const Endpoint& to, RejectReason reason, std::uint64_t clientSalt) {
    std::vector<std::uint8_t> packet;
    ByteWriter writer(packet);
    WriteHandshakeHeader(writer, wire::kTypeReject);
    writer.U8(static_cast<std::uint8_t>(reason));
    writer.U64(clientSalt);
    writer.U32(config_.identity.release);
    writer.U64(config_.identity.contentHash);
    if (transport_->Send(to, packet) == TransportStatus::Error) ++stats_.sendErrors;
}

void Host::HandleConnect(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    if (datagram.size() < kMinConnectBytes) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "short Connect");
        return;
    }
    ByteReader reader(datagram.subspan(kHandshakeHeaderBytes));
    const std::uint64_t clientId = reader.U64();
    const std::uint64_t clientSalt = reader.U64();
    const BuildIdentity identity{ reader.U32(), reader.U64() };
    if (identity.release < config_.minClientRelease) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeVersion, "client release is below minimum");
        SendReject(from, RejectReason::OutdatedClient, clientSalt);
        return;
    }
    if (identity.contentHash != 0U && config_.identity.contentHash != 0U && identity.contentHash != config_.identity.contentHash) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "content hashes differ");
        SendReject(from, RejectReason::ContentMismatch, clientSalt);
        return;
    }
    SessionKey key{};
    if (!reader.Ok() || clientId == 0U || !keys_->LookupKey(clientId, key)) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "unknown client in Connect");
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
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid Response length");
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
        diagnostics_.Report(DiagCode::HandshakeContent, "unknown client in Response");
        return;
    }

    // The cookie proves this address received the Challenge; the MAC proves the sender holds the key.
    const std::uint64_t epoch = nowMilliseconds / config_.cookieLifetimeMilliseconds;
    const bool cookieOk = serverSalt == Cookie(from, clientId, clientSalt, epoch) ||
        (epoch != 0U && serverSalt == Cookie(from, clientId, clientSalt, epoch - 1U));
    const Mac expected = ComputeMac(key, "21kb-net-response", clientId, clientSalt, serverSalt);
    if (!cookieOk || !kb::security::ConstantTimeEqual(expected, receivedMac)) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid Response MAC or expired address cookie");
        kb::security::SecureWipe(key);
        return;
    }

    const auto existing = peers_.find(from);
    if (existing != peers_.end()) {
        if (existing->second.clientId == clientId && existing->second.clientSalt == clientSalt && existing->second.serverSalt == serverSalt) {
            if (transport_->Send(from, existing->second.accept) == TransportStatus::Error) ++stats_.sendErrors;
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
        diagnostics_.Report(DiagCode::HandshakeServerFull, "server client limit reached");
        SendReject(from, RejectReason::ServerFull, clientSalt);
        kb::security::SecureWipe(key);
        return;
    }

    std::array<std::uint8_t, 32U> serverRandom{};
    kb::security::AeadNonce nonce{};
    if (!kb::security::SecureRandom(serverRandom) || !kb::security::SecureRandom(nonce)) {
        kb::security::SecureWipe(key);
        kb::security::SecureWipe(serverRandom);
        diagnostics_.Report(DiagCode::HandshakeContent, "secure random source unavailable");
        return;
    }
    Peer peer;
    peer.endpoint = from;
    peer.clientId = clientId;
    peer.clientSalt = clientSalt;
    peer.serverSalt = serverSalt;
    peer.createdAt = nowMilliseconds;
    ByteWriter accept(peer.accept);
    WriteHandshakeHeader(accept, wire::kTypeAccept);
    accept.Bytes(nonce);
    accept.Bytes(serverRandom);
    accept.U32(config_.identity.release);
    accept.U64(config_.identity.contentHash);
    kb::security::AeadKey acceptKey;
    DeriveAcceptKey(key, clientSalt, serverSalt, acceptKey);
    kb::security::AeadTag tag{};
    kb::security::AeadEncryptInPlace(std::span<std::uint8_t>(peer.accept).subspan(kAcceptPrefixBytes), tag,
        acceptKey, nonce, std::span<const std::uint8_t>(peer.accept).first(kAcceptPrefixBytes));
    accept.Bytes(tag);
    SessionKey clientToServer{};
    SessionKey serverToClient{};
    DeriveSessionKeys(key, clientSalt, serverSalt, serverRandom, clientToServer, serverToClient);
    kb::security::SecureWipe(serverRandom);
    kb::security::SecureWipe(key);
    if (transport_->Send(from, peer.accept) == TransportStatus::Error) ++stats_.sendErrors;
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
        diagnostics_.Report(DiagCode::RateLimited, "peer packet limit reached");
        return;
    }
    ++peer.packetsThisUpdate;
    if (peer.session->ProcessPacket(nowMilliseconds, datagram) == PacketResult::Accepted) peer.confirmed = true;
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

std::vector<std::uint64_t> Host::ClientIds() const {
    std::vector<std::uint64_t> ids;
    ids.reserve(byClient_.size());
    for (const auto& entry : byClient_) {
        ids.push_back(entry.first);
    }
    return ids;
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
    if (config_.identity.release == 0U && config_.identity.contentHash == 0U) {
        diagnostics_.Report(DiagCode::VersionUnchecked, "client build identity is unchecked");
    }
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
    writer.U32(config_.identity.release);
    writer.U64(config_.identity.contentHash);
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
    std::uint8_t version = 0U;
    if (!ReadHeaderPrefix(reader, version, type) || version != wire::kVersion) {
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
        stage_ = Stage::Response;
        SendResponse();
        lastHandshakeSend_ = nowMilliseconds;
        return;
    }
    if (type == wire::kTypeAccept && stage_ == Stage::Response && datagram.size() == kAcceptBytes) {
        kb::security::AeadNonce nonce{};
        const auto receivedNonce = reader.Bytes(nonce.size());
        std::copy(receivedNonce.begin(), receivedNonce.end(), nonce.begin());
        std::array<std::uint8_t, kAcceptPlainBytes> plaintext{};
        const auto ciphertext = reader.Bytes(plaintext.size());
        std::copy(ciphertext.begin(), ciphertext.end(), plaintext.begin());
        kb::security::AeadTag tag{};
        const auto receivedTag = reader.Bytes(tag.size());
        std::copy(receivedTag.begin(), receivedTag.end(), tag.begin());
        kb::security::AeadKey acceptKey;
        DeriveAcceptKey(key_, clientSalt_, serverSalt_, acceptKey);
        if (!kb::security::AeadDecryptInPlace(plaintext, tag, acceptKey, nonce, datagram.first(kAcceptPrefixBytes))) return;
        ByteReader payload(plaintext);
        const auto random = payload.Bytes(32U);
        const BuildIdentity identity{ payload.U32(), payload.U64() };
        if (identity.contentHash != 0U && config_.identity.contentHash != 0U &&
            identity.contentHash != config_.identity.contentHash) {
            kb::security::SecureWipe(plaintext);
            Fail(DisconnectReason::ContentMismatch);
            return;
        }
        SessionKey clientToServer{};
        SessionKey serverToClient{};
        DeriveSessionKeys(key_, clientSalt_, serverSalt_, std::span<const std::uint8_t, 32U>(random.data(), 32U), clientToServer, serverToClient);
        kb::security::SecureWipe(plaintext);
        session_ = std::make_unique<Session>(config_.session, clientToServer, serverToClient, nowMilliseconds);
        kb::security::SecureWipe(clientToServer);
        kb::security::SecureWipe(serverToClient);
        stage_ = Stage::Established;
        state_ = ClientState::Connected;
        NetEvent event;
        event.type = NetEventType::Connected;
        events_.push_back(std::move(event));
        return;
    }
    if (type == wire::kTypeReject && state_ == ClientState::Connecting && datagram.size() == kRejectBytes) {
        const auto reason = static_cast<RejectReason>(reader.U8());
        const std::uint64_t echoedSalt = reader.U64();
        const BuildIdentity identity{ reader.U32(), reader.U64() };
        if (!reader.Ok() || echoedSalt != clientSalt_) return;
        DisconnectReason disconnect = DisconnectReason::None;
        switch (reason) {
        case RejectReason::ProtocolVersion:
        case RejectReason::OutdatedClient: disconnect = DisconnectReason::VersionMismatch; break;
        case RejectReason::ContentMismatch: disconnect = DisconnectReason::ContentMismatch; break;
        case RejectReason::ServerFull: disconnect = DisconnectReason::ServerFull; break;
        default: return;
        }
        rejected_ = true;
        rejectInfo_ = { reason, identity };
        Fail(disconnect);
        return;
    }
    if (type == wire::kTypeData && stage_ == Stage::Established && session_ != nullptr) {
        static_cast<void>(session_->ProcessPacket(nowMilliseconds, datagram));
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
            if (state_ == ClientState::Disconnected) return;
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
    if (state_ != ClientState::Connected) return SendResult::Closed;
    if (!config_.authorizationRequired) return session_->Send(channel, data);
    if (data.size() >= config_.session.maxMessageBytes) return SendResult::TooLarge;
    return session_->Send(channel, MakeCommandMessage(data));
}

SendResult Client::Authenticate(std::string_view token) {
    if (state_ != ClientState::Connected || !config_.authorizationRequired) return SendResult::Closed;
    if (token.size() > kMaxTokenBytes) return SendResult::TooLarge;
    return session_->Send(0U, MakeLoginMessage(token));
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
