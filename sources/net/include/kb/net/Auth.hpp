#pragma once

#include "engine/security/Crypto.hpp"
#include "kb/net/Host.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Who is connected, and what they may do: signed tokens, a revocation list, a sample login provider and the server gate that ties
// them to Host.
//
// The model. A login provider (a service the game operator runs, outside the game server) checks who a player is however it likes
// and issues a token: a short, signed statement "this client id is this subject, with these scopes, from this time until that
// time". The token is signed with Ed25519, so a game server only needs the provider's public key to trust it and never sees a
// password. The provider also hands the client a session key and registers it with the server's key table (Host.hpp describes that
// step; this layer does not change it). The client sends the token as its first message; the AuthGate on the server checks it and
// only then lets that peer's commands through. A token ends when it expires, or earlier when the provider revokes it: the
// provider publishes a signed revocation list and the server loads it; the gate disconnects the peers it names.
//
// What it is and is not. It is an authentication and authorisation layer for the commands of a game server that already
// validates them (NetworkSecurity.hpp). It is not an anti-cheat, and it does not replace server-side validation. Revocation takes
// effect when the server has loaded the new list, so the delay is the interval at which the server fetches it. Clocks: tokens
// carry seconds of the provider's wall clock; the server's clock must be within the allowed skew.
//
// LocalAuthProvider is a real provider for tests, tools and small deployments: accounts with salted, stretched password
// verifiers, lock-out after repeated failures, signed tokens and revocation. It stores accounts in memory only. A game with real
// players would put a provider behind its own account system; the token format and the gate stay the same.
namespace kb::net {

// Scopes are a bitmask a game defines for itself. These are conventions the samples and tests use.
inline constexpr std::uint32_t kScopePlay = 1U << 0U;
inline constexpr std::uint32_t kScopeChat = 1U << 1U;
inline constexpr std::uint32_t kScopeModerate = 1U << 2U;
inline constexpr std::uint32_t kScopeAdmin = 1U << 3U;

inline constexpr std::size_t kMaxTokenBytes = 1024U;
inline constexpr std::size_t kMaxSubjectBytes = 64U;

struct AuthClaims {
    std::uint64_t tokenId = 0U;  // unique per token, for revocation
    std::uint64_t clientId = 0U; // the client id the token is good for (Host.hpp)
    std::string subject;         // who: 1 to kMaxSubjectBytes printable characters
    std::int64_t issuedAt = 0;   // seconds since the Unix epoch, by the provider's clock
    std::int64_t expiresAt = 0;
    std::uint32_t scopes = 0U;

    [[nodiscard]] bool operator==(const AuthClaims&) const = default;
};

enum class AuthStatus : std::uint8_t {
    Ok,
    Malformed,          // not a token of this format
    UnsupportedVersion,
    BadSignature,       // not signed by this provider, or changed after signing
    Expired,
    NotYetValid,        // issued in the future beyond the allowed skew
    Revoked,
    MissingScope,
    WrongClient,        // the token is for another client id
};

[[nodiscard]] std::string_view AuthStatusName(AuthStatus status) noexcept;
[[nodiscard]] bool IsValidAuthClaims(const AuthClaims& claims) noexcept;

// "kb1." + base64url(payload) + "." + base64url(signature). Empty for claims that are not valid.
[[nodiscard]] std::string IssueToken(const AuthClaims& claims, const kb::security::Ed25519SecretKey& secretKey);

struct AuthVerification {
    AuthStatus status = AuthStatus::Malformed;
    AuthClaims claims{};
};

// Checks the format, the signature and the time. Revocation and scopes are the caller's (RevocationList, AuthGate).
[[nodiscard]] AuthVerification VerifyToken(std::string_view token, const kb::security::Ed25519PublicKey& publicKey,
    std::int64_t nowSeconds, std::int64_t allowedSkewSeconds = 30);

// Tokens the provider has withdrawn: single tokens, and everything issued to a subject up to a time.
class RevocationList final {
public:
    void RevokeToken(std::uint64_t tokenId);
    // Every token of `subject` issued at or before `issuedUpTo` (seconds).
    void RevokeSubject(const std::string& subject, std::int64_t issuedUpTo);
    [[nodiscard]] bool IsRevoked(const AuthClaims& claims) const;
    [[nodiscard]] std::size_t Size() const noexcept { return tokens_.size() + subjects_.size(); }

    // A snapshot signed by the provider, for the servers to load. `publishedAt` orders snapshots.
    [[nodiscard]] std::vector<std::uint8_t> Serialize(const kb::security::Ed25519SecretKey& secretKey, std::int64_t publishedAt) const;
    // False, leaving `out` as it was, for a list that is malformed, not signed by this key or has too many entries.
    [[nodiscard]] static bool Parse(std::span<const std::uint8_t> bytes, const kb::security::Ed25519PublicKey& publicKey,
        RevocationList& out, std::int64_t* publishedAt = nullptr);

private:
    std::set<std::uint64_t> tokens_;
    std::map<std::string, std::int64_t> subjects_;
};

struct LocalAuthConfig {
    std::int64_t tokenLifetimeSeconds = 900;
    std::uint32_t maxFailedLogins = 5U;     // in a row, per account, before it is locked
    std::int64_t lockSeconds = 60;
    std::uint32_t passwordIterations = 20'000U; // PBKDF2-HMAC-SHA512 rounds
};

struct LoginResult {
    bool ok = false;
    std::string error; // the reason, never which half of the credentials was wrong
    std::string token;
    SessionKey sessionKey{};
    std::uint64_t clientId = 0U;
    AuthClaims claims{};
};

class LocalAuthProvider final {
public:
    // The signing key comes from `seed`; the same seed is the same provider.
    explicit LocalAuthProvider(const kb::security::Ed25519Seed& seed, LocalAuthConfig config = {});

    [[nodiscard]] const kb::security::Ed25519PublicKey& PublicKey() const noexcept { return publicKey_; }
    // False for a name that is empty, over kMaxSubjectBytes or taken, a password under 8 bytes, or when no random bytes came.
    [[nodiscard]] bool AddAccount(const std::string& name, std::string_view password, std::uint32_t scopes);
    // A token and a fresh session key for the account; the key is also put in Keys() for the server. A new login replaces the
    // previous session key of the account (Host.hpp: Replaced).
    [[nodiscard]] LoginResult Login(const std::string& name, std::string_view password, std::int64_t nowSeconds);
    // The table a Host reads session keys from.
    [[nodiscard]] const StaticKeyTable& Keys() const noexcept { return keys_; }

    void RevokeToken(std::uint64_t tokenId);
    // Withdraws every token the account has at `nowSeconds` or earlier, and its session key. A token issued in the same second
    // counts as earlier; the account logs in again from the next second.
    void RevokeAccount(const std::string& name, std::int64_t nowSeconds);
    // RevokeAccount, and the account can no longer log in.
    void BanAccount(const std::string& name, std::int64_t nowSeconds);
    [[nodiscard]] const RevocationList& Revocations() const noexcept { return revocations_; }
    [[nodiscard]] std::vector<std::uint8_t> PublishRevocations(std::int64_t nowSeconds) const;

private:
    struct Account {
        std::uint64_t clientId = 0U;
        std::array<std::uint8_t, 16U> salt{};
        kb::security::Sha512Digest verifier{};
        std::uint32_t scopes = 0U;
        std::uint32_t failures = 0U;
        std::int64_t lockedUntil = 0;
        bool banned = false;
    };

    LocalAuthConfig config_;
    kb::security::Ed25519SecretKey secretKey_;
    kb::security::Ed25519PublicKey publicKey_{};
    std::map<std::string, Account> accounts_;
    StaticKeyTable keys_;
    RevocationList revocations_;
    std::uint64_t nextClientId_ = 1U;
    std::uint64_t nextTokenId_ = 1U;
};

// Message framing between a client and the AuthGate: the first byte says what the rest is.
inline constexpr std::uint8_t kAuthMessageLogin = 0xA1U;   // client -> server: the token
inline constexpr std::uint8_t kAuthMessageCommand = 0xA2U; // client -> server: an application command
// What the server answers: one type byte and a 4 byte little endian value.
inline constexpr std::uint8_t kAuthMessageAccepted = 0xB1U; // logged in; the scopes
inline constexpr std::uint8_t kAuthMessageRefused = 0xB2U;  // the login was refused; the AuthStatus
inline constexpr std::uint8_t kAuthMessageDenied = 0xB3U;   // a command was refused; the scopes that were missing (0: not logged in)
inline constexpr std::uint8_t kAuthMessageKicked = 0xB4U;   // the session is ending; the AuthKickReason

[[nodiscard]] std::vector<std::uint8_t> MakeLoginMessage(std::string_view token);
[[nodiscard]] std::vector<std::uint8_t> MakeCommandMessage(std::span<const std::uint8_t> command);

enum class AuthKickReason : std::uint8_t {
    Expired,
    Revoked,
    LoginTimeout,     // connected and never logged in
    TooManyViolations, // commands before login, or commands the scopes do not allow
};

struct AuthGateConfig {
    kb::security::Ed25519PublicKey providerKey{};
    std::int64_t allowedSkewSeconds = 30;
    std::uint32_t loginTimeoutMilliseconds = 5000U;
    std::uint32_t maxViolations = 3U;        // refused commands before the peer is dropped
    std::size_t maxCommandBytes = 1024U;     // larger commands are violations
    std::uint32_t maxCommandsPerUpdate = 64U; // per peer; the rest are violations
    std::uint8_t channel = 0U;               // the channel the gate sends its answers on
};

struct AuthGateHooks {
    std::function<void(std::uint64_t peer, const AuthClaims& claims)> onAuthenticated;
    std::function<void(std::uint64_t peer, AuthStatus status)> onLoginRefused;
    std::function<void(std::uint64_t peer, std::uint32_t missingScopes)> onCommandDenied;
    std::function<void(std::uint64_t peer, AuthKickReason reason)> onKicked;
};

// Stands between a Host and the game server's command handling. Feed it the Host's events; what comes out is the commands of
// peers that have logged in with a valid token. Everything else is answered, counted and, when it keeps happening, ends the
// connection.
class AuthGate final {
public:
    AuthGate(Host& host, AuthGateConfig config, AuthGateHooks hooks = {});

    // Loads a signed revocation list from the provider; false when it is not valid or older than the one already loaded.
    [[nodiscard]] bool UpdateRevocations(std::span<const std::uint8_t> bytes);
    // Reads the Host's events, answers logins, drops peers whose token ended, and appends the commands of logged-in peers to
    // `commands` as Message events (peer = client id, message.data = the command).
    void Update(std::uint64_t nowMilliseconds, std::int64_t nowSeconds, std::vector<NetEvent>& commands);
    // Whether the peer holds all of `requiredScopes`; if not, the peer is told, the hook runs and it counts as a violation.
    [[nodiscard]] bool Authorize(std::uint64_t peer, std::uint32_t requiredScopes);

    [[nodiscard]] const AuthClaims* Claims(std::uint64_t peer) const noexcept;
    [[nodiscard]] std::size_t AuthenticatedCount() const noexcept;
    struct Stats {
        std::uint64_t loginsAccepted = 0U;
        std::uint64_t loginsRefused = 0U;
        std::uint64_t commandsPassed = 0U;
        std::uint64_t commandsRefused = 0U;
        std::uint64_t kicked = 0U;
    };
    [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }

private:
    struct PeerState {
        std::uint64_t connectedAt = 0U;
        bool authenticated = false;
        AuthClaims claims{};
        std::uint32_t violations = 0U;
        std::uint32_t commandsThisUpdate = 0U;
    };

    void Kick(std::uint64_t peer, AuthKickReason reason);
    void Violation(std::uint64_t peer, PeerState& state);
    void HandleLogin(std::uint64_t peer, PeerState& state, std::span<const std::uint8_t> body, std::int64_t nowSeconds);
    void Reply(std::uint64_t peer, std::uint8_t type, std::uint32_t value);

    Host* host_;
    AuthGateConfig config_;
    AuthGateHooks hooks_;
    RevocationList revocations_;
    std::int64_t revocationsPublishedAt_ = -1;
    std::map<std::uint64_t, PeerState> peers_;
    std::vector<std::uint64_t> closing_; // kicked peers whose connection ends on the next update
    Stats stats_;
};

} // namespace kb::net
