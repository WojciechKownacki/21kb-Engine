#include "kb/net/Auth.hpp"

#include "kb/net/ByteIO.hpp"

#include <algorithm>
#include <array>

namespace kb::net {
namespace {

constexpr std::string_view kTokenPrefix = "kb";
constexpr std::string_view kTokenDomain = "21kb.net.auth.token.v1";
constexpr std::string_view kRevocationDomain = "21kb.net.auth.revocations.v1";
constexpr std::uint8_t kTokenVersion = 1U;
constexpr std::int64_t kMaxLifetimeSeconds = 30LL * 24LL * 3600LL;
constexpr std::size_t kMaxRevokedTokens = 100'000U;
constexpr std::size_t kMaxRevokedSubjects = 10'000U;
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] std::string Base64UrlEncode(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve((bytes.size() * 4U + 2U) / 3U);
    std::size_t index = 0U;
    for (; index + 3U <= bytes.size(); index += 3U) {
        const std::uint32_t block = (static_cast<std::uint32_t>(bytes[index]) << 16U) | (static_cast<std::uint32_t>(bytes[index + 1U]) << 8U) | bytes[index + 2U];
        for (int shift = 18; shift >= 0; shift -= 6) text.push_back(kAlphabet[(block >> shift) & 63U]);
    }
    const std::size_t rest = bytes.size() - index;
    if (rest == 1U) {
        const std::uint32_t block = static_cast<std::uint32_t>(bytes[index]) << 16U;
        text.push_back(kAlphabet[(block >> 18U) & 63U]);
        text.push_back(kAlphabet[(block >> 12U) & 63U]);
    } else if (rest == 2U) {
        const std::uint32_t block = (static_cast<std::uint32_t>(bytes[index]) << 16U) | (static_cast<std::uint32_t>(bytes[index + 1U]) << 8U);
        text.push_back(kAlphabet[(block >> 18U) & 63U]);
        text.push_back(kAlphabet[(block >> 12U) & 63U]);
        text.push_back(kAlphabet[(block >> 6U) & 63U]);
    }
    return text;
}

[[nodiscard]] int Base64UrlValue(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

// False for anything but canonical unpadded base64url.
[[nodiscard]] bool Base64UrlDecode(std::string_view text, std::vector<std::uint8_t>& out) {
    out.clear();
    if (text.size() % 4U == 1U) return false;
    std::uint32_t block = 0U;
    unsigned bits = 0U;
    for (const char c : text) {
        const int value = Base64UrlValue(c);
        if (value < 0) return false;
        block = (block << 6U) | static_cast<std::uint32_t>(value);
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            out.push_back(static_cast<std::uint8_t>((block >> bits) & 0xFFU));
        }
    }
    // The unused low bits must be zero, so one token has exactly one spelling.
    return (block & ((1U << bits) - 1U)) == 0U;
}

[[nodiscard]] std::vector<std::uint8_t> WithDomain(std::string_view domain, std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> message(domain.begin(), domain.end());
    message.push_back(0U);
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

[[nodiscard]] bool IsPrintableName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxSubjectBytes) return false;
    return std::ranges::all_of(name, [](char c) { return static_cast<unsigned char>(c) >= 0x20U && static_cast<unsigned char>(c) != 0x7FU; });
}

// RFC 8018 PBKDF2 with HMAC-SHA512, one output block.
[[nodiscard]] kb::security::Sha512Digest Pbkdf2(std::string_view password, std::span<const std::uint8_t> salt, std::uint32_t iterations) {
    const std::span<const std::uint8_t> key{ reinterpret_cast<const std::uint8_t*>(password.data()), password.size() };
    std::vector<std::uint8_t> first(salt.begin(), salt.end());
    first.insert(first.end(), { 0U, 0U, 0U, 1U });
    kb::security::Sha512Digest u = kb::security::HmacSha512(key, first);
    kb::security::Sha512Digest t = u;
    for (std::uint32_t round = 1U; round < iterations; ++round) {
        u = kb::security::HmacSha512(key, u);
        for (std::size_t index = 0U; index < t.size(); ++index) t[index] ^= u[index];
    }
    return t;
}

[[nodiscard]] std::uint64_t ToUnsigned(std::int64_t value) noexcept { return static_cast<std::uint64_t>(value); }
[[nodiscard]] std::int64_t ToSigned(std::uint64_t value) noexcept { return static_cast<std::int64_t>(value); }

} // namespace

std::string_view AuthStatusName(AuthStatus status) noexcept {
    switch (status) {
    case AuthStatus::Ok: return "ok";
    case AuthStatus::Malformed: return "malformed";
    case AuthStatus::UnsupportedVersion: return "unsupported version";
    case AuthStatus::BadSignature: return "bad signature";
    case AuthStatus::Expired: return "expired";
    case AuthStatus::NotYetValid: return "not yet valid";
    case AuthStatus::Revoked: return "revoked";
    case AuthStatus::MissingScope: return "missing scope";
    case AuthStatus::WrongClient: return "wrong client";
    }
    return "unknown";
}

bool IsValidAuthClaims(const AuthClaims& claims) noexcept {
    return claims.tokenId != 0U && claims.clientId != 0U && IsPrintableName(claims.subject) && claims.issuedAt > 0 &&
        claims.expiresAt > claims.issuedAt && claims.expiresAt - claims.issuedAt <= kMaxLifetimeSeconds;
}

std::string IssueToken(const AuthClaims& claims, const kb::security::Ed25519SecretKey& secretKey) {
    if (!IsValidAuthClaims(claims)) return {};
    std::vector<std::uint8_t> payload;
    ByteWriter writer(payload);
    writer.U8(kTokenVersion);
    writer.U64(claims.tokenId);
    writer.U64(claims.clientId);
    writer.U64(ToUnsigned(claims.issuedAt));
    writer.U64(ToUnsigned(claims.expiresAt));
    writer.U32(claims.scopes);
    writer.U8(static_cast<std::uint8_t>(claims.subject.size()));
    writer.Bytes({ reinterpret_cast<const std::uint8_t*>(claims.subject.data()), claims.subject.size() });
    const kb::security::Ed25519Signature signature = kb::security::Ed25519Sign(secretKey, WithDomain(kTokenDomain, payload));
    return std::string{ kTokenPrefix } + "1." + Base64UrlEncode(payload) + "." + Base64UrlEncode(signature);
}

AuthVerification VerifyToken(std::string_view token, const kb::security::Ed25519PublicKey& publicKey, std::int64_t nowSeconds, std::int64_t allowedSkewSeconds) {
    AuthVerification result;
    if (token.size() > kMaxTokenBytes || token.size() < 6U || token.substr(0U, 2U) != kTokenPrefix || token[3] != '.') return result;
    if (token[2] < '0' || token[2] > '9') return result;
    if (token[2] != '1') {
        result.status = AuthStatus::UnsupportedVersion;
        return result;
    }
    const std::string_view rest = token.substr(4U);
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos || rest.find('.', dot + 1U) != std::string_view::npos) return result;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> signatureBytes;
    if (!Base64UrlDecode(rest.substr(0U, dot), payload) || !Base64UrlDecode(rest.substr(dot + 1U), signatureBytes) ||
        signatureBytes.size() != kb::security::kEd25519SignatureBytes) {
        return result;
    }
    kb::security::Ed25519Signature signature{};
    std::copy(signatureBytes.begin(), signatureBytes.end(), signature.begin());
    if (!kb::security::Ed25519Verify(signature, publicKey, WithDomain(kTokenDomain, payload))) {
        result.status = AuthStatus::BadSignature;
        return result;
    }
    ByteReader reader(payload);
    if (reader.U8() != kTokenVersion) {
        result.status = AuthStatus::UnsupportedVersion;
        return result;
    }
    AuthClaims claims;
    claims.tokenId = reader.U64();
    claims.clientId = reader.U64();
    claims.issuedAt = ToSigned(reader.U64());
    claims.expiresAt = ToSigned(reader.U64());
    claims.scopes = reader.U32();
    const std::size_t subjectBytes = reader.U8();
    const std::span<const std::uint8_t> subject = reader.Bytes(subjectBytes);
    if (!reader.Ok() || reader.Remaining() != 0U) return result;
    claims.subject.assign(reinterpret_cast<const char*>(subject.data()), subject.size());
    if (!IsValidAuthClaims(claims)) return result;
    result.claims = claims;
    if (claims.issuedAt > nowSeconds + allowedSkewSeconds) {
        result.status = AuthStatus::NotYetValid;
        return result;
    }
    if (nowSeconds >= claims.expiresAt) {
        result.status = AuthStatus::Expired;
        return result;
    }
    result.status = AuthStatus::Ok;
    return result;
}

void RevocationList::RevokeToken(std::uint64_t tokenId) { tokens_.insert(tokenId); }

void RevocationList::RevokeSubject(const std::string& subject, std::int64_t issuedUpTo) {
    std::int64_t& limit = subjects_[subject];
    limit = std::max(limit, issuedUpTo);
}

bool RevocationList::IsRevoked(const AuthClaims& claims) const {
    if (tokens_.contains(claims.tokenId)) return true;
    const auto found = subjects_.find(claims.subject);
    return found != subjects_.end() && claims.issuedAt <= found->second;
}

std::vector<std::uint8_t> RevocationList::Serialize(const kb::security::Ed25519SecretKey& secretKey, std::int64_t publishedAt) const {
    std::vector<std::uint8_t> bytes;
    ByteWriter writer(bytes);
    writer.U32(0x4C52424BU); // "KBRL"
    writer.U8(1U);
    writer.U64(ToUnsigned(publishedAt));
    writer.U32(static_cast<std::uint32_t>(tokens_.size()));
    for (const std::uint64_t id : tokens_) writer.U64(id);
    writer.U32(static_cast<std::uint32_t>(subjects_.size()));
    for (const auto& [subject, limit] : subjects_) {
        writer.U8(static_cast<std::uint8_t>(subject.size()));
        writer.Bytes({ reinterpret_cast<const std::uint8_t*>(subject.data()), subject.size() });
        writer.U64(ToUnsigned(limit));
    }
    const kb::security::Ed25519Signature signature = kb::security::Ed25519Sign(secretKey, WithDomain(kRevocationDomain, bytes));
    writer.Bytes(signature);
    return bytes;
}

bool RevocationList::Parse(std::span<const std::uint8_t> bytes, const kb::security::Ed25519PublicKey& publicKey, RevocationList& out, std::int64_t* publishedAt) {
    if (bytes.size() < kb::security::kEd25519SignatureBytes + 4U + 1U + 8U + 4U + 4U || bytes.size() > 8U * 1024U * 1024U) return false;
    const std::span<const std::uint8_t> body = bytes.first(bytes.size() - kb::security::kEd25519SignatureBytes);
    kb::security::Ed25519Signature signature{};
    std::copy(bytes.end() - static_cast<std::ptrdiff_t>(signature.size()), bytes.end(), signature.begin());
    if (!kb::security::Ed25519Verify(signature, publicKey, WithDomain(kRevocationDomain, body))) return false;
    ByteReader reader(body);
    if (reader.U32() != 0x4C52424BU || reader.U8() != 1U) return false;
    const std::int64_t published = ToSigned(reader.U64());
    RevocationList parsed;
    const std::uint32_t tokenCount = reader.U32();
    if (tokenCount > kMaxRevokedTokens || static_cast<std::size_t>(tokenCount) * 8U > reader.Remaining()) return false;
    for (std::uint32_t index = 0U; index < tokenCount; ++index) parsed.tokens_.insert(reader.U64());
    const std::uint32_t subjectCount = reader.U32();
    if (subjectCount > kMaxRevokedSubjects) return false;
    for (std::uint32_t index = 0U; index < subjectCount; ++index) {
        const std::size_t length = reader.U8();
        const std::span<const std::uint8_t> name = reader.Bytes(length);
        const std::int64_t limit = ToSigned(reader.U64());
        if (!reader.Ok() || length == 0U) return false;
        parsed.subjects_[std::string(reinterpret_cast<const char*>(name.data()), name.size())] = limit;
    }
    if (!reader.Ok() || reader.Remaining() != 0U) return false;
    out = std::move(parsed);
    if (publishedAt != nullptr) *publishedAt = published;
    return true;
}

LocalAuthProvider::LocalAuthProvider(const kb::security::Ed25519Seed& seed, LocalAuthConfig config) : config_(config) {
    kb::security::Ed25519KeyPairFromSeed(seed, secretKey_, publicKey_);
    config_.passwordIterations = std::max<std::uint32_t>(1U, config_.passwordIterations);
}

bool LocalAuthProvider::AddAccount(const std::string& name, std::string_view password, std::uint32_t scopes) {
    if (!IsPrintableName(name) || password.size() < 8U || accounts_.contains(name)) return false;
    Account account;
    if (!kb::security::SecureRandom(account.salt)) return false;
    account.verifier = Pbkdf2(password, account.salt, config_.passwordIterations);
    account.clientId = nextClientId_++;
    account.scopes = scopes;
    accounts_.emplace(name, std::move(account));
    return true;
}

LoginResult LocalAuthProvider::Login(const std::string& name, std::string_view password, std::int64_t nowSeconds) {
    LoginResult result;
    const auto found = accounts_.find(name);
    // An unknown name costs the same work as a wrong password, and answers the same.
    if (found == accounts_.end()) {
        static constexpr std::array<std::uint8_t, 16U> kDummySalt{ 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U };
        static_cast<void>(Pbkdf2(password, kDummySalt, config_.passwordIterations));
        result.error = "the name or the password is wrong";
        return result;
    }
    Account& account = found->second;
    if (account.banned) {
        result.error = "the account is banned";
        return result;
    }
    if (nowSeconds < account.lockedUntil) {
        result.error = "the account is locked after too many wrong passwords; try again later";
        return result;
    }
    const kb::security::Sha512Digest attempt = Pbkdf2(password, account.salt, config_.passwordIterations);
    if (!kb::security::ConstantTimeEqual(attempt, account.verifier)) {
        if (++account.failures >= config_.maxFailedLogins) {
            account.failures = 0U;
            account.lockedUntil = nowSeconds + config_.lockSeconds;
        }
        result.error = "the name or the password is wrong";
        return result;
    }
    account.failures = 0U;
    SessionKey key{};
    if (!kb::security::SecureRandom(key)) {
        result.error = "the provider could not draw a session key";
        return result;
    }
    AuthClaims claims;
    claims.tokenId = nextTokenId_++;
    claims.clientId = account.clientId;
    claims.subject = name;
    claims.issuedAt = nowSeconds;
    claims.expiresAt = nowSeconds + config_.tokenLifetimeSeconds;
    claims.scopes = account.scopes;
    result.token = IssueToken(claims, secretKey_);
    if (result.token.empty()) {
        result.error = "the provider's time or token lifetime is not valid";
        return result;
    }
    keys_.Set(account.clientId, key);
    result.ok = true;
    result.sessionKey = key;
    result.clientId = account.clientId;
    result.claims = claims;
    return result;
}

void LocalAuthProvider::RevokeToken(std::uint64_t tokenId) { revocations_.RevokeToken(tokenId); }

void LocalAuthProvider::RevokeAccount(const std::string& name, std::int64_t nowSeconds) {
    const auto found = accounts_.find(name);
    if (found == accounts_.end()) return;
    revocations_.RevokeSubject(name, nowSeconds);
    keys_.Remove(found->second.clientId);
}

void LocalAuthProvider::BanAccount(const std::string& name, std::int64_t nowSeconds) {
    const auto found = accounts_.find(name);
    if (found == accounts_.end()) return;
    found->second.banned = true;
    RevokeAccount(name, nowSeconds);
}

std::vector<std::uint8_t> LocalAuthProvider::PublishRevocations(std::int64_t nowSeconds) const { return revocations_.Serialize(secretKey_, nowSeconds); }

std::vector<std::uint8_t> MakeLoginMessage(std::string_view token) {
    std::vector<std::uint8_t> message;
    message.reserve(token.size() + 1U);
    message.push_back(kAuthMessageLogin);
    message.insert(message.end(), token.begin(), token.end());
    return message;
}

std::vector<std::uint8_t> MakeCommandMessage(std::span<const std::uint8_t> command) {
    std::vector<std::uint8_t> message;
    message.reserve(command.size() + 1U);
    message.push_back(kAuthMessageCommand);
    message.insert(message.end(), command.begin(), command.end());
    return message;
}

AuthGate::AuthGate(Host& host, AuthGateConfig config, AuthGateHooks hooks) : host_(&host), config_(config), hooks_(std::move(hooks)) {}

bool AuthGate::UpdateRevocations(std::span<const std::uint8_t> bytes) {
    RevocationList parsed;
    std::int64_t publishedAt = 0;
    if (!RevocationList::Parse(bytes, config_.providerKey, parsed, &publishedAt) || publishedAt < revocationsPublishedAt_) return false;
    revocations_ = std::move(parsed);
    revocationsPublishedAt_ = publishedAt;
    return true;
}

void AuthGate::Reply(std::uint64_t peer, std::uint8_t type, std::uint32_t value) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(type);
    writer.U32(value);
    static_cast<void>(host_->Send(peer, config_.channel, message));
}

void AuthGate::Kick(std::uint64_t peer, AuthKickReason reason) {
    Reply(peer, kAuthMessageKicked, static_cast<std::uint32_t>(reason));
    ++stats_.kicked;
    if (hooks_.onKicked) hooks_.onKicked(peer, reason);
    // The notice above goes out with the next Host update; a Disconnect asked for now would replace it. The connection is closed on
    // the update after that.
    closing_.push_back(peer);
    peers_.erase(peer);
}

void AuthGate::Violation(std::uint64_t peer, PeerState& state) {
    ++state.violations;
    if (state.violations >= config_.maxViolations) Kick(peer, AuthKickReason::TooManyViolations);
}

void AuthGate::HandleLogin(std::uint64_t peer, PeerState& state, std::span<const std::uint8_t> body, std::int64_t nowSeconds) {
    const std::string_view token{ reinterpret_cast<const char*>(body.data()), body.size() };
    AuthVerification verification = VerifyToken(token, config_.providerKey, nowSeconds, config_.allowedSkewSeconds);
    if (verification.status == AuthStatus::Ok && verification.claims.clientId != peer) verification.status = AuthStatus::WrongClient;
    if (verification.status == AuthStatus::Ok && revocations_.IsRevoked(verification.claims)) verification.status = AuthStatus::Revoked;
    if (verification.status != AuthStatus::Ok) {
        ++stats_.loginsRefused;
        Reply(peer, kAuthMessageRefused, static_cast<std::uint32_t>(verification.status));
        if (hooks_.onLoginRefused) hooks_.onLoginRefused(peer, verification.status);
        Violation(peer, state);
        return;
    }
    state.authenticated = true;
    state.claims = verification.claims;
    ++stats_.loginsAccepted;
    Reply(peer, kAuthMessageAccepted, state.claims.scopes);
    if (hooks_.onAuthenticated) hooks_.onAuthenticated(peer, state.claims);
}

void AuthGate::Update(std::uint64_t nowMilliseconds, std::int64_t nowSeconds, std::vector<NetEvent>& commands) {
    for (const std::uint64_t peer : closing_) host_->Disconnect(peer);
    closing_.clear();
    for (auto& [peer, state] : peers_) {
        static_cast<void>(peer);
        state.commandsThisUpdate = 0U;
    }
    NetEvent event;
    while (host_->PollEvent(event)) {
        const std::uint64_t peer = event.peer;
        if (event.type == NetEventType::Connected) {
            PeerState state;
            state.connectedAt = nowMilliseconds;
            peers_[peer] = state;
            continue;
        }
        if (event.type == NetEventType::Disconnected) {
            peers_.erase(peer);
            continue;
        }
        const auto found = peers_.find(peer);
        if (found == peers_.end()) continue; // a peer already dropped
        PeerState& state = found->second;
        const std::vector<std::uint8_t>& data = event.message.data;
        if (data.empty()) {
            ++stats_.commandsRefused;
            Violation(peer, state);
            continue;
        }
        const std::span<const std::uint8_t> body{ data.data() + 1U, data.size() - 1U };
        if (data[0] == kAuthMessageLogin) {
            if (body.size() > kMaxTokenBytes) {
                ++stats_.loginsRefused;
                Reply(peer, kAuthMessageRefused, static_cast<std::uint32_t>(AuthStatus::Malformed));
                Violation(peer, state);
            } else {
                HandleLogin(peer, state, body, nowSeconds);
            }
            continue;
        }
        if (data[0] == kAuthMessageCommand) {
            if (!state.authenticated || body.size() > config_.maxCommandBytes || ++state.commandsThisUpdate > config_.maxCommandsPerUpdate) {
                ++stats_.commandsRefused;
                if (!state.authenticated) Reply(peer, kAuthMessageDenied, 0U);
                Violation(peer, state);
                continue;
            }
            ++stats_.commandsPassed;
            NetEvent command;
            command.type = NetEventType::Message;
            command.peer = peer;
            command.message.channel = event.message.channel;
            command.message.data.assign(body.begin(), body.end());
            commands.push_back(std::move(command));
            continue;
        }
        ++stats_.commandsRefused;
        Violation(peer, state);
    }

    // Tokens that ended, logins that never came.
    std::vector<std::pair<std::uint64_t, AuthKickReason>> ending;
    for (const auto& [peer, state] : peers_) {
        if (!state.authenticated) {
            if (nowMilliseconds - state.connectedAt >= config_.loginTimeoutMilliseconds) ending.emplace_back(peer, AuthKickReason::LoginTimeout);
        } else if (nowSeconds >= state.claims.expiresAt) {
            ending.emplace_back(peer, AuthKickReason::Expired);
        } else if (revocations_.IsRevoked(state.claims)) {
            ending.emplace_back(peer, AuthKickReason::Revoked);
        }
    }
    for (const auto& [peer, reason] : ending) Kick(peer, reason);
}

bool AuthGate::Authorize(std::uint64_t peer, std::uint32_t requiredScopes) {
    const auto found = peers_.find(peer);
    if (found == peers_.end()) return false;
    PeerState& state = found->second;
    const std::uint32_t missing = state.authenticated ? (requiredScopes & ~state.claims.scopes) : requiredScopes;
    if (state.authenticated && missing == 0U) return true;
    ++stats_.commandsRefused;
    Reply(peer, kAuthMessageDenied, missing);
    if (hooks_.onCommandDenied) hooks_.onCommandDenied(peer, missing);
    Violation(peer, state);
    return false;
}

const AuthClaims* AuthGate::Claims(std::uint64_t peer) const noexcept {
    const auto found = peers_.find(peer);
    return found != peers_.end() && found->second.authenticated ? &found->second.claims : nullptr;
}

std::size_t AuthGate::AuthenticatedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(peers_, [](const auto& entry) { return entry.second.authenticated; }));
}

} // namespace kb::net
