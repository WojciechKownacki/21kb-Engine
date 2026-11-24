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
constexpr std::size_t kAcceptPlainBytes = 76U;
constexpr std::size_t kAcceptPrefixBytes = kHandshakeHeaderBytes + kb::security::kAeadNonceBytes;
constexpr std::size_t kRejectBytes = 25U;
constexpr std::size_t kResponseBytes = kHandshakeHeaderBytes + 8U + 8U + 8U + 32U + kMacBytes;
constexpr std::size_t kPasswordChallengeBytes = 84U;
constexpr std::size_t kPasswordFinishBytes = 52U;
constexpr std::size_t kOpenChallengeBytes = 28U;
constexpr std::size_t kOpenAcceptPlainBytes = kAcceptPlainBytes + 9U;
static_assert(kChallengeBytes < kMinConnectBytes, "a Challenge must be smaller than the Connect that asks for it");

constexpr std::size_t kMaxQueuedEvents = 4096U;
constexpr std::size_t kClientReceiveLimit = 256U;
constexpr std::size_t kClientPacketsPerUpdate = 16U;

using Mac = std::array<std::uint8_t, kMacBytes>;

[[nodiscard]] std::span<const std::uint8_t> AsBytes(std::string_view text) noexcept {
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

[[nodiscard]] Mac ComputeMac(const SessionKey& key, std::string_view label, std::uint64_t clientId, std::uint64_t clientSalt,
    std::uint64_t serverSalt, std::span<const std::uint8_t, 32U> clientEphemeral) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.Bytes(AsBytes(label));
    writer.U64(clientId);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    writer.Bytes(clientEphemeral);
    const kb::security::Sha512Digest digest = kb::security::HmacSha512(key, message);
    Mac mac{};
    std::copy_n(digest.begin(), mac.size(), mac.begin());
    return mac;
}

void DeriveSessionKeys(const SessionKey& key, std::uint64_t clientSalt, std::uint64_t serverSalt, std::span<const std::uint8_t, 32U> serverRandom,
    const kb::security::SecretBytes<32U>& shared, SessionKey& clientToServer, SessionKey& serverToClient,
    std::span<const std::uint8_t> isk = {}) {
    std::vector<std::uint8_t> salt;
    ByteWriter writer(salt);
    writer.U64(clientSalt);
    writer.U64(serverSalt);
    writer.Bytes(serverRandom);
    std::array<std::uint8_t, 2U * kSessionKeyBytes> output{};
    kb::security::SecretBytes<128U> material;
    std::copy(key.begin(), key.end(), material.data());
    std::copy(shared.Span().begin(), shared.Span().end(), material.data() + key.size());
    std::copy(isk.begin(), isk.end(), material.data() + 64U);
    kb::security::HkdfSha512(output, material.Span().first(64U + isk.size()), salt, AsBytes("21kb-net-session-v2"));
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

const StaticKeyTable& NoKeys() { static const StaticKeyTable keys; return keys; }

std::vector<std::uint8_t> PasswordSid(std::uint64_t clientSalt, std::uint64_t serverSalt) {
    std::vector<std::uint8_t> sid;
    ByteWriter writer(sid); writer.U64(clientSalt); writer.U64(serverSalt);
    return sid;
}

SessionKey PasswordKey(const kb::security::SecretBytes<64U>& isk) {
    SessionKey key{};
    kb::security::HkdfSha512(key, isk.Span(), {}, AsBytes("21kb-net-cpace-confirm"));
    return key;
}

Mac PasswordProof(const SessionKey& key, std::span<const std::uint8_t> response, std::span<const std::uint8_t> challenge) {
    kb::security::HmacSha512Hasher hash(key);
    hash.Update(AsBytes("21kb-net-cpace-client")); hash.Update(response); hash.Update(challenge);
    auto digest = hash.Finish();
    Mac proof{}; std::copy_n(digest.begin(), proof.size(), proof.begin());
    kb::security::SecureWipe(digest);
    return proof;
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

Host::Host(IDatagramTransport& transport, HostConfig config) : Host(transport, std::move(config), NoKeys()) {}

Host::Host(IDatagramTransport& transport, HostConfig config, const IKeyProvider& keys)
    : transport_(&transport), keys_(&keys), config_(std::move(config)) {
    scratch_.reserve(kMaxDatagramBytes);
    valid_ = config_.session.IsValid() && config_.maxClients != 0U && config_.maxDatagramsPerUpdate != 0U &&
        config_.maxPacketsPerPeerPerUpdate != 0U && config_.cookieLifetimeMilliseconds != 0U && config_.confirmMilliseconds != 0U &&
        config_.maxConnectsPerSecondPerAddress != 0U && config_.maxSessionsPerAddress != 0U &&
        kb::security::SecureRandom(cookieSecret_);
    if (config_.admission == Admission::JoinPassword && config_.hostSalt == std::array<std::uint8_t, 8U>{})
        valid_ = valid_ && kb::security::SecureRandom(config_.hostSalt);
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

SessionKey Host::DecoyKey(std::uint64_t clientId) const {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.Bytes(AsBytes("21kb-net-decoy"));
    writer.U64(clientId);
    const kb::security::Sha512Digest digest = kb::security::HmacSha512(cookieSecret_, message);
    SessionKey key{};
    std::copy_n(digest.begin(), key.size(), key.begin());
    return key;
}

bool Host::AllowConnect(std::uint64_t nowMilliseconds, std::uint32_t address) {
    constexpr std::size_t kMaxBuckets = 8192U;
    constexpr std::uint64_t kSweepMilliseconds = 10'000U;
    const double rate = static_cast<double>(config_.maxConnectsPerSecondPerAddress);
    const auto refilled = [&](const ConnectBucket& bucket) {
        return std::min(rate, bucket.tokens + static_cast<double>(nowMilliseconds - bucket.updatedAt) * rate / 1000.0);
    };
    const auto sweep = [&] {
        lastBucketSweep_ = nowMilliseconds;
        for (auto it = connectBuckets_.begin(); it != connectBuckets_.end();) {
            it = refilled(it->second) >= rate ? connectBuckets_.erase(it) : std::next(it);
        }
    };
    if (nowMilliseconds - lastBucketSweep_ >= kSweepMilliseconds) sweep();
    auto found = connectBuckets_.find(address);
    if (found == connectBuckets_.end()) {
        if (connectBuckets_.size() >= kMaxBuckets) sweep();
        if (connectBuckets_.size() >= kMaxBuckets) {
            ++stats_.connectBucketsFull;
            diagnostics_.Report(DiagCode::ConnectBucketsFull, config_.admission == Admission::JoinPassword ? "password exchange refused: address bucket table is full" : "authenticated Response admitted without an address bucket: table is full");
            return config_.admission != Admission::JoinPassword;
        }
        found = connectBuckets_.emplace(address, ConnectBucket{ rate, nowMilliseconds }).first;
    }
    ConnectBucket& bucket = found->second;
    bucket.tokens = refilled(bucket);
    bucket.updatedAt = nowMilliseconds;
    if (bucket.tokens < 1.0) return false;
    bucket.tokens -= 1.0;
    return true;
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
        case wire::kTypePasswordFinish: HandlePasswordFinish(nowMilliseconds, from, datagram); break;
        case wire::kTypeData: HandleData(nowMilliseconds, from, datagram); break;
        default: ++stats_.malformed; break;
        }
    }

    for (auto iterator = passwordExchanges_.begin(); iterator != passwordExchanges_.end();) {
        iterator = nowMilliseconds - iterator->second.createdAt >= config_.confirmMilliseconds ? passwordExchanges_.erase(iterator) : std::next(iterator);
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
            if (peer.session->NextDueMs(nowMilliseconds) > nowMilliseconds || !peer.session->BuildPacket(nowMilliseconds, scratch_)) {
                break;
            }
            if (transport_->Send(peer.endpoint, scratch_) == TransportStatus::Error) {
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
    const auto ephemeral = reader.Bytes(32U);
    const auto admission = static_cast<Admission>(reader.U8());
    if (admission != config_.admission) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "admission mode mismatch");
        return;
    }
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
    if (!reader.Ok() || (clientId == 0U && config_.admission == Admission::KeyTable)) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid client id in Connect");
        return;
    }
    if (config_.admission == Admission::JoinPassword) {
        std::vector<std::uint8_t> challenge;
        ByteWriter writer(challenge); WriteHandshakeHeader(writer, wire::kTypeChallenge);
        writer.U64(clientSalt);
        writer.U64(Cookie(from, clientId, clientSalt, nowMilliseconds / config_.cookieLifetimeMilliseconds));
        writer.Bytes(config_.hostSalt);
        if (transport_->Send(from, challenge) == TransportStatus::Error) ++stats_.sendErrors;
        ++stats_.challengesSent;
        return;
    }
    // An unknown id gets the same Challenge, made from a key only this host can compute: its Response cannot pass, and nobody learns which ids exist.
    SessionKey decoy = DecoyKey(clientId);
    SessionKey key{};
    if (!keys_->LookupKey(clientId, key)) {
        key = decoy;
    }
    kb::security::SecureWipe(decoy);
    const std::uint64_t serverSalt = Cookie(from, clientId, clientSalt, nowMilliseconds / config_.cookieLifetimeMilliseconds);
    const Mac mac = ComputeMac(key, "21kb-net-challenge", clientId, clientSalt, serverSalt, std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U));
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
    if (config_.admission == Admission::JoinPassword) { HandlePasswordResponse(nowMilliseconds, from, datagram); return; }
    if (datagram.size() != kResponseBytes) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid Response length");
        return;
    }
    ByteReader reader(datagram.subspan(kHandshakeHeaderBytes));
    const std::uint64_t clientId = reader.U64();
    const std::uint64_t clientSalt = reader.U64();
    const std::uint64_t serverSalt = reader.U64();
    const auto ephemeral = reader.Bytes(32U);
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
    const Mac expected = ComputeMac(key, "21kb-net-response", clientId, clientSalt, serverSalt, std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U));
    if (!cookieOk || !kb::security::ConstantTimeEqual(expected, receivedMac)) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid Response MAC or expired address cookie");
        kb::security::SecureWipe(key);
        return;
    }

    if (!AllowConnect(nowMilliseconds, from.address)) {
        ++stats_.handshakesRateLimited;
        diagnostics_.Report(DiagCode::HandshakeRateLimited, "Response rate limit per address reached");
        kb::security::SecureWipe(key);
        return;
    }

    const auto existing = peers_.find(from);
    if (existing != peers_.end()) {
        if (existing->second.clientId == clientId && existing->second.clientSalt == clientSalt && existing->second.serverSalt == serverSalt &&
            std::equal(ephemeral.begin(), ephemeral.end(), existing->second.clientEphemeral.begin())) {
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
    std::size_t sameAddress = 0U;
    for (auto it = peers_.lower_bound(Endpoint{ from.address, 0U }); it != peers_.end() && it->first.address == from.address; ++it) ++sameAddress;
    if (sameAddress >= config_.maxSessionsPerAddress) {
        ++stats_.sessionsPerAddressRefused;
        diagnostics_.Report(DiagCode::HandshakeServerFull, "session limit per address reached");
        SendReject(from, RejectReason::ServerFull, clientSalt);
        kb::security::SecureWipe(key);
        return;
    }
    if (peers_.size() >= config_.maxClients) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeServerFull, "server client limit reached");
        SendReject(from, RejectReason::ServerFull, clientSalt);
        kb::security::SecureWipe(key);
        return;
    }

    kb::security::X25519KeyPair serverEphemeral;
    kb::security::SecretBytes<32U> shared;
    if (!kb::security::GenerateX25519KeyPair(serverEphemeral) || !kb::security::X25519SharedSecret(serverEphemeral.secret,
        std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U), shared)) {
        kb::security::SecureWipe(key);
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeContent, "invalid ephemeral public key or random source unavailable");
        return;
    }
    CompleteHandshake(nowMilliseconds, from, clientId, clientSalt, serverSalt,
        std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U), serverEphemeral.publicKey, key, shared);
    kb::security::SecureWipe(key);
}

void Host::CompleteHandshake(std::uint64_t nowMilliseconds, const Endpoint& from, std::uint64_t clientId,
    std::uint64_t clientSalt, std::uint64_t serverSalt, std::span<const std::uint8_t, 32U> ephemeral,
    std::span<const std::uint8_t, 32U> serverEphemeral, const SessionKey& key, const kb::security::SecretBytes<32U>& shared,
    std::span<const std::uint8_t> isk, std::span<const std::uint8_t> response, std::span<const std::uint8_t> finish) {
    std::array<std::uint8_t, 32U> serverRandom{};
    kb::security::AeadNonce nonce{};
    if (!kb::security::SecureRandom(serverRandom) || !kb::security::SecureRandom(nonce)) {
        kb::security::SecureWipe(serverRandom);
        diagnostics_.Report(DiagCode::HandshakeContent, "secure random source unavailable");
        return;
    }
    Peer peer;
    std::copy(ephemeral.begin(), ephemeral.end(), peer.clientEphemeral.begin());
    peer.response.assign(response.begin(), response.end());
    peer.finish.assign(finish.begin(), finish.end());
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
    accept.Bytes(serverEphemeral);
    if (config_.admission == Admission::JoinPassword) {
        accept.U64(clientId);
        accept.U8(config_.joinPassword.empty() ? 0U : 1U);
    }
    kb::security::AeadKey acceptKey;
    DeriveAcceptKey(key, clientSalt, serverSalt, acceptKey);
    kb::security::AeadTag tag{};
    kb::security::AeadEncryptInPlace(std::span<std::uint8_t>(peer.accept).subspan(kAcceptPrefixBytes), tag,
        acceptKey, nonce, std::span<const std::uint8_t>(peer.accept).first(kAcceptPrefixBytes));
    accept.Bytes(tag);
    SessionKey clientToServer{};
    SessionKey serverToClient{};
    DeriveSessionKeys(key, clientSalt, serverSalt, serverRandom, shared, clientToServer, serverToClient, isk);
    kb::security::SecureWipe(serverRandom);
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

void Host::HandlePasswordResponse(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    if (datagram.size() != kResponseBytes) { ++stats_.handshakesRejected; diagnostics_.Report(DiagCode::HandshakeContent, "invalid password Response length"); return; }
    ByteReader reader(datagram.subspan(kHandshakeHeaderBytes));
    const auto claimedId = reader.U64(), clientSalt = reader.U64(), serverSalt = reader.U64();
    const auto ephemeral = reader.Bytes(32U), ya = reader.Bytes(32U);
    const auto epoch = nowMilliseconds / config_.cookieLifetimeMilliseconds;
    if (serverSalt != Cookie(from, claimedId, clientSalt, epoch) &&
        (epoch == 0U || serverSalt != Cookie(from, claimedId, clientSalt, epoch - 1U))) {
        ++stats_.handshakesRejected; diagnostics_.Report(DiagCode::HandshakeContent, "invalid password address cookie"); return;
    }
    if (const auto peer = peers_.find(from); peer != peers_.end() && std::ranges::equal(peer->second.response, datagram)) {
        if (transport_->Send(from, peer->second.accept) == TransportStatus::Error) ++stats_.sendErrors;
        return;
    }
    if (const auto pending = passwordExchanges_.find(from); pending != passwordExchanges_.end() && std::ranges::equal(pending->second.response, datagram)) {
        if (transport_->Send(from, pending->second.challenge) == TransportStatus::Error) ++stats_.sendErrors;
        return;
    }
    // Every new PAKE exchange consumes a token only after proving address ownership. A full table fails closed here.
    if (!AllowConnect(nowMilliseconds, from.address)) {
        ++stats_.passwordRateLimited;
        diagnostics_.Report(DiagCode::PasswordRateLimited, "password attempts per address exhausted");
        return;
    }
    std::size_t sameAddress = 0U;
    for (const auto& peer : peers_) if (peer.first.address == from.address && peer.first != from) ++sameAddress;
    for (const auto& pending : passwordExchanges_) if (pending.first.address == from.address && pending.first != from) ++sameAddress;
    if (sameAddress >= config_.maxSessionsPerAddress || (passwordExchanges_.size() >= config_.maxClients && !passwordExchanges_.contains(from))) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeServerFull, "password exchange capacity reached");
        SendReject(from, RejectReason::ServerFull, clientSalt); return;
    }
    ++stats_.passwordAttempts;
    PasswordExchange exchange;
    exchange.createdAt = nowMilliseconds;
    exchange.response.assign(datagram.begin(), datagram.end());
    kb::security::X25519KeyPair ephemeralPair;
    kb::security::SecretBytes<32U> scalar, publicPoint, shared;
    const auto sid = PasswordSid(clientSalt, serverSalt);
    auto generator = kb::security::CPaceGenerator(config_.joinPassword, config_.hostSalt, sid);
    const bool ok = kb::security::SecureRandom(scalar.Span()) && kb::security::X25519SharedSecret(scalar, generator, publicPoint) &&
        kb::security::X25519SharedSecret(scalar, std::span<const std::uint8_t, 32U>(ya.data(), 32U), shared) &&
        kb::security::GenerateX25519KeyPair(ephemeralPair) && kb::security::X25519SharedSecret(ephemeralPair.secret,
            std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U), exchange.shared);
    kb::security::SecureWipe(generator);
    if (!ok) { ++stats_.handshakesRejected; diagnostics_.Report(DiagCode::HandshakeContent, "invalid password group element or random source unavailable"); return; }
    exchange.serverEphemeral = ephemeralPair.publicKey;
    ByteWriter writer(exchange.challenge);
    WriteHandshakeHeader(writer, wire::kTypePasswordChallenge);
    writer.U64(clientSalt); writer.U64(serverSalt);
    writer.Bytes(publicPoint.Span()); writer.Bytes(exchange.serverEphemeral);
    kb::security::CPaceISK(exchange.isk, sid, shared, std::span<const std::uint8_t, 32U>(ya.data(), 32U),
        datagram.first(60U), publicPoint.Span(), std::span(exchange.challenge).subspan(52U));
    if (transport_->Send(from, exchange.challenge) == TransportStatus::Error) ++stats_.sendErrors;
    passwordExchanges_.insert_or_assign(from, std::move(exchange));
}

void Host::HandlePasswordFinish(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram) {
    if (config_.admission != Admission::JoinPassword || datagram.size() != kPasswordFinishBytes) {
        ++stats_.handshakesRejected; diagnostics_.Report(DiagCode::HandshakeContent, "invalid password confirmation"); return;
    }
    if (const auto peer = peers_.find(from); peer != peers_.end() && kb::security::ConstantTimeEqual(peer->second.finish, datagram)) {
        if (transport_->Send(from, peer->second.accept) == TransportStatus::Error) ++stats_.sendErrors;
        return;
    }
    const auto pending = passwordExchanges_.find(from);
    if (pending == passwordExchanges_.end()) { ++stats_.handshakesRejected; diagnostics_.Report(DiagCode::HandshakeContent, "password confirmation has no exchange"); return; }
    auto& exchange = pending->second;
    auto key = PasswordKey(exchange.isk);
    const auto expected = PasswordProof(key, exchange.response, exchange.challenge);
    const bool valid = nowMilliseconds - exchange.createdAt < config_.confirmMilliseconds &&
        std::ranges::equal(datagram.subspan(4U, 16U), std::span(exchange.challenge).subspan(4U, 16U)) &&
        kb::security::ConstantTimeEqual(expected, datagram.last(kMacBytes));
    ByteReader response(std::span(exchange.response).subspan(4U));
    static_cast<void>(response.U64());
    const auto clientSalt = response.U64(), serverSalt = response.U64();
    const auto ephemeral = response.Bytes(32U);
    if (!valid) {
        ++stats_.passwordRejected; ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::PasswordRejected, "password confirmation rejected online");
        SendReject(from, RejectReason::PasswordRejected, clientSalt);
    } else if (peers_.size() >= config_.maxClients && !peers_.contains(from)) {
        ++stats_.handshakesRejected;
        diagnostics_.Report(DiagCode::HandshakeServerFull, "server client limit reached");
        SendReject(from, RejectReason::ServerFull, clientSalt);
    } else {
        std::array<std::uint8_t, 8U> random{};
        std::uint64_t assigned = 0U;
        do {
            if (!kb::security::SecureRandom(random)) { kb::security::SecureWipe(key); passwordExchanges_.erase(pending); return; }
            ByteReader id(random); assigned = id.U64();
        } while (assigned <= kHostPlayerId || byClient_.contains(assigned));
        if (const auto peer = peers_.find(from); peer != peers_.end()) RemovePeer(peer, DisconnectReason::Requested);
        CompleteHandshake(nowMilliseconds, from, assigned, clientSalt, serverSalt,
            std::span<const std::uint8_t, 32U>(ephemeral.data(), 32U), exchange.serverEphemeral, key, exchange.shared,
            exchange.isk.Span(), exchange.response, datagram);
        if (config_.joinPassword.empty() && byClient_.contains(assigned))
            diagnostics_.Report(DiagCode::UnauthenticatedGuests, "encrypted connection accepted without guest authentication");
    }
    kb::security::SecureWipe(key);
    passwordExchanges_.erase(pending);
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

Client::Client(IDatagramTransport& transport, ClientConfig config, Endpoint host, std::uint64_t nowMilliseconds)
    : Client(transport, std::move(config), 0U, SessionKey{}, host, nowMilliseconds) {}

Client::Client(IDatagramTransport& transport, ClientConfig config, std::uint64_t clientId, const SessionKey& key, Endpoint host,
    std::uint64_t nowMilliseconds)
    : transport_(&transport), config_(std::move(config)), clientId_(clientId), key_(key), host_(host), clientSalt_(config_.clientSalt),
      startedAt_(nowMilliseconds), lastHandshakeSend_(nowMilliseconds) {
    scratch_.reserve(kMaxDatagramBytes);
    if (config_.identity.release == 0U && config_.identity.contentHash == 0U) {
        diagnostics_.Report(DiagCode::VersionUnchecked, "client build identity is unchecked");
    }
    if (!config_.session.IsValid() || (clientId_ == 0U && config_.admission == Admission::KeyTable) || config_.retryMilliseconds == 0U) {
        Fail(DisconnectReason::Error);
        return;
    }
    if (!kb::security::GenerateX25519KeyPair(ephemeral_)) {
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
    kb::security::SecureWipe(ephemeral_.secret.Span());
    kb::security::SecureWipe(passwordScalar_.Span());
    kb::security::SecureWipe(isk_.Span());
    kb::security::SecureWipe(key_);
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
    writer.Bytes(ephemeral_.publicKey);
    writer.U8(static_cast<std::uint8_t>(config_.admission));
    writer.Zeros(kMinConnectBytes - packet.size());
    static_cast<void>(transport_->Send(host_, packet));
}

void Client::SendResponse() {
    if (config_.admission == Admission::JoinPassword) {
        if (stage_ == Stage::PasswordFinish) { static_cast<void>(transport_->Send(host_, passwordFinish_)); return; }
        if (passwordResponse_.empty()) {
            ByteWriter writer(passwordResponse_); WriteHandshakeHeader(writer, wire::kTypeResponse);
            writer.U64(clientId_); writer.U64(clientSalt_); writer.U64(serverSalt_);
            writer.Bytes(ephemeral_.publicKey); writer.Bytes(passwordPublic_);
        }
        static_cast<void>(transport_->Send(host_, passwordResponse_));
        return;
    }
    const Mac mac = ComputeMac(key_, "21kb-net-response", clientId_, clientSalt_, serverSalt_, ephemeral_.publicKey);
    std::vector<std::uint8_t> packet;
    ByteWriter writer(packet);
    WriteHandshakeHeader(writer, wire::kTypeResponse);
    writer.U64(clientId_);
    writer.U64(clientSalt_);
    writer.U64(serverSalt_);
    writer.Bytes(ephemeral_.publicKey);
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
        if (config_.admission == Admission::JoinPassword) {
            if (stage_ != Stage::Connect || datagram.size() != kOpenChallengeBytes) return;
            const auto echoedSalt = reader.U64(), cookie = reader.U64();
            const auto hostSalt = reader.Bytes(8U);
            if (!reader.Ok() || echoedSalt != clientSalt_) return;
            const auto sid = PasswordSid(clientSalt_, cookie);
            auto generator = kb::security::CPaceGenerator(config_.joinPassword, hostSalt, sid);
            kb::security::SecretBytes<32U> point;
            const bool ok = kb::security::SecureRandom(passwordScalar_.Span()) && kb::security::X25519SharedSecret(passwordScalar_, generator, point);
            kb::security::SecureWipe(generator);
            if (!ok) { Fail(DisconnectReason::Error); return; }
            std::copy(point.Span().begin(), point.Span().end(), passwordPublic_.begin());
            serverSalt_ = cookie; stage_ = Stage::Response;
            SendResponse(); lastHandshakeSend_ = nowMilliseconds;
            return;
        }
        if (stage_ != Stage::Connect || datagram.size() != kChallengeBytes) {
            return;
        }
        const std::uint64_t echoedSalt = reader.U64();
        const std::uint64_t serverSalt = reader.U64();
        const std::span<const std::uint8_t> receivedMac = reader.Bytes(kMacBytes);
        if (!reader.Ok() || echoedSalt != clientSalt_) {
            return;
        }
        const Mac expected = ComputeMac(key_, "21kb-net-challenge", clientId_, clientSalt_, serverSalt, ephemeral_.publicKey);
        if (!kb::security::ConstantTimeEqual(expected, receivedMac)) {
            return; // the host does not know the key: not the host we meant to reach
        }
        serverSalt_ = serverSalt;
        stage_ = Stage::Response;
        SendResponse();
        lastHandshakeSend_ = nowMilliseconds;
        return;
    }
    if (type == wire::kTypePasswordChallenge && config_.admission == Admission::JoinPassword && stage_ == Stage::Response) {
        if (datagram.size() != kPasswordChallengeBytes || reader.U64() != clientSalt_ || reader.U64() != serverSalt_) return;
        const auto yb = reader.Bytes(32U);
        kb::security::SecretBytes<32U> shared;
        if (!kb::security::X25519SharedSecret(passwordScalar_, std::span<const std::uint8_t, 32U>(yb.data(), 32U), shared)) { Fail(DisconnectReason::Error); return; }
        kb::security::CPaceISK(isk_, PasswordSid(clientSalt_, serverSalt_), shared, passwordPublic_,
            std::span(passwordResponse_).first(60U), std::span<const std::uint8_t, 32U>(yb.data(), 32U), datagram.subspan(52U));
        kb::security::SecureWipe(passwordScalar_.Span());
        key_ = PasswordKey(isk_);
        const auto proof = PasswordProof(key_, passwordResponse_, datagram);
        ByteWriter writer(passwordFinish_); WriteHandshakeHeader(writer, wire::kTypePasswordFinish);
        writer.U64(clientSalt_); writer.U64(serverSalt_); writer.Bytes(proof);
        stage_ = Stage::PasswordFinish;
        SendResponse(); lastHandshakeSend_ = nowMilliseconds;
        return;
    }
    const bool password = config_.admission == Admission::JoinPassword;
    const auto acceptPlainBytes = password ? kOpenAcceptPlainBytes : kAcceptPlainBytes;
    if (type == wire::kTypeAccept && stage_ == (password ? Stage::PasswordFinish : Stage::Response) &&
        datagram.size() == kAcceptPrefixBytes + acceptPlainBytes + kb::security::kAeadTagBytes) {
        kb::security::AeadNonce nonce{};
        const auto receivedNonce = reader.Bytes(nonce.size());
        std::copy(receivedNonce.begin(), receivedNonce.end(), nonce.begin());
        std::array<std::uint8_t, kOpenAcceptPlainBytes> plaintext{};
        const auto ciphertext = reader.Bytes(acceptPlainBytes);
        std::copy(ciphertext.begin(), ciphertext.end(), plaintext.begin());
        kb::security::AeadTag tag{};
        const auto receivedTag = reader.Bytes(tag.size());
        std::copy(receivedTag.begin(), receivedTag.end(), tag.begin());
        kb::security::AeadKey acceptKey;
        DeriveAcceptKey(key_, clientSalt_, serverSalt_, acceptKey);
        if (!kb::security::AeadDecryptInPlace(std::span(plaintext).first(acceptPlainBytes), tag, acceptKey, nonce, datagram.first(kAcceptPrefixBytes))) return;
        ByteReader payload(std::span(plaintext).first(acceptPlainBytes));
        const auto random = payload.Bytes(32U);
        const BuildIdentity identity{ payload.U32(), payload.U64() };
        if (identity.contentHash != 0U && config_.identity.contentHash != 0U &&
            identity.contentHash != config_.identity.contentHash) {
            kb::security::SecureWipe(plaintext);
            Fail(DisconnectReason::ContentMismatch);
            return;
        }
        const auto publicKey = payload.Bytes(32U);
        if (password) {
            const auto assigned = payload.U64();
            const auto authenticated = payload.U8();
            if (!payload.Ok() || assigned <= kHostPlayerId || authenticated != (config_.joinPassword.empty() ? 0U : 1U)) { Fail(DisconnectReason::Error); return; }
            clientId_ = assigned;
            guestAuthenticated_ = authenticated != 0U;
        } else guestAuthenticated_ = true;
        kb::security::SecretBytes<32U> shared;
        if (!kb::security::X25519SharedSecret(ephemeral_.secret,
            std::span<const std::uint8_t, 32U>(publicKey.data(), 32U), shared)) {
            kb::security::SecureWipe(plaintext);
            diagnostics_.Report(DiagCode::HandshakeContent, "invalid server ephemeral public key");
            Fail(DisconnectReason::Error);
            return;
        }
        SessionKey clientToServer{};
        SessionKey serverToClient{};
        DeriveSessionKeys(key_, clientSalt_, serverSalt_, std::span<const std::uint8_t, 32U>(random.data(), 32U), shared, clientToServer, serverToClient,
            password ? std::span<const std::uint8_t>(isk_.Span()) : std::span<const std::uint8_t>{});
        kb::security::SecureWipe(isk_.Span());
        kb::security::SecureWipe(key_);
        kb::security::SecureWipe(ephemeral_.secret.Span());
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
        case RejectReason::PasswordRejected: if (!password || stage_ != Stage::PasswordFinish) return; disconnect = DisconnectReason::PasswordRejected; break;
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
        if (session_->NextDueMs(nowMilliseconds) > nowMilliseconds || !session_->BuildPacket(nowMilliseconds, scratch_)) {
            break;
        }
        static_cast<void>(transport_->Send(host_, scratch_));
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
