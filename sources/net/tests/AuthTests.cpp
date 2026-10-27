// Tests for the login layer of kb_net: signed tokens, revocation, the sample provider and the server gate, over the simulated
// network and over real UDP sockets. Deterministic: time is passed in, the fuzz generator is fixed.

#include "kb/net/Auth.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace kb::net;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

constexpr std::int64_t kBase = 1'700'000'000;

kb::security::Ed25519Seed MakeSeed(std::uint8_t value) {
    kb::security::Ed25519Seed seed;
    for (std::size_t index = 0U; index < seed.size(); ++index) {
        seed.data()[index] = static_cast<std::uint8_t>(value + index * 3U);
    }
    return seed;
}

LocalAuthConfig FastConfig() {
    LocalAuthConfig config;
    config.passwordIterations = 40U;
    config.tokenLifetimeSeconds = 60;
    config.maxFailedLogins = 3U;
    config.lockSeconds = 30;
    return config;
}

AuthClaims SampleClaims() {
    AuthClaims claims;
    claims.tokenId = 77U;
    claims.clientId = 5U;
    claims.subject = "ada";
    claims.issuedAt = kBase;
    claims.expiresAt = kBase + 600;
    claims.scopes = kScopePlay | kScopeChat;
    return claims;
}

struct Random {
    std::uint64_t state;
    std::uint64_t Next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }
};

// ------------------------------------------------------------------ tokens

void TestTokenFormat() {
    const kb::security::Ed25519Seed seed = MakeSeed(1U);
    kb::security::Ed25519SecretKey secret;
    kb::security::Ed25519PublicKey publicKey{};
    kb::security::Ed25519KeyPairFromSeed(seed, secret, publicKey);

    const AuthClaims claims = SampleClaims();
    const std::string token = IssueToken(claims, secret);
    Require(!token.empty() && token.rfind("kb1.", 0U) == 0U && token.size() < kMaxTokenBytes, "a valid token was not issued");
    AuthVerification checked = VerifyToken(token, publicKey, kBase + 10);
    Require(checked.status == AuthStatus::Ok && checked.claims == claims, "a valid token did not verify to its claims");
    Require(IssueToken(claims, secret) == token, "the same claims did not give the same token");

    // The time window: valid until the second it expires, and a little before it was issued.
    Require(VerifyToken(token, publicKey, kBase + 599).status == AuthStatus::Ok, "a token was refused one second before it expires");
    Require(VerifyToken(token, publicKey, kBase + 600).status == AuthStatus::Expired, "a token was accepted at its expiry");
    Require(VerifyToken(token, publicKey, kBase + 5000).status == AuthStatus::Expired, "an old token was accepted");
    Require(VerifyToken(token, publicKey, kBase - 20).status == AuthStatus::Ok, "clock skew inside the allowance was refused");
    Require(VerifyToken(token, publicKey, kBase - 31).status == AuthStatus::NotYetValid, "a token from the future was accepted");
    Require(VerifyToken(token, publicKey, kBase - 31, 60).status == AuthStatus::Ok, "the skew allowance was not honoured");

    // Another provider's key never verifies it.
    kb::security::Ed25519SecretKey otherSecret;
    kb::security::Ed25519PublicKey otherPublic{};
    kb::security::Ed25519KeyPairFromSeed(MakeSeed(2U), otherSecret, otherPublic);
    Require(VerifyToken(token, otherPublic, kBase + 10).status == AuthStatus::BadSignature, "a token verified under another key");
    Require(VerifyToken(IssueToken(claims, otherSecret), publicKey, kBase + 10).status == AuthStatus::BadSignature, "a forged token verified");

    // Claims that are not valid are never signed.
    AuthClaims bad = claims;
    bad.subject.clear();
    Require(IssueToken(bad, secret).empty(), "a token without a subject was issued");
    bad = claims;
    bad.subject = std::string(kMaxSubjectBytes + 1U, 'x');
    Require(IssueToken(bad, secret).empty(), "a token with a long subject was issued");
    bad = claims;
    bad.subject = "tab\there";
    Require(IssueToken(bad, secret).empty(), "a token with a control character was issued");
    bad = claims;
    bad.expiresAt = bad.issuedAt;
    Require(IssueToken(bad, secret).empty(), "a token that expires when it is issued was issued");
    bad = claims;
    bad.expiresAt = bad.issuedAt + 31LL * 24LL * 3600LL;
    Require(IssueToken(bad, secret).empty(), "a token that lives over thirty days was issued");
    bad = claims;
    bad.tokenId = 0U;
    Require(IssueToken(bad, secret).empty(), "a token without an id was issued");
    bad = claims;
    bad.clientId = 0U;
    Require(IssueToken(bad, secret).empty(), "a token without a client was issued");

    // Any change of a single character breaks it, and nothing crashes on garbage.
    for (std::size_t index = 0U; index < token.size(); ++index) {
        std::string changed = token;
        changed[index] = changed[index] == 'A' ? 'B' : 'A';
        if (changed == token) continue;
        Require(VerifyToken(changed, publicKey, kBase + 10).status != AuthStatus::Ok, "a changed token verified (character " + std::to_string(index) + ")");
    }
    Require(VerifyToken("kb2." + token.substr(4U), publicKey, kBase + 10).status == AuthStatus::UnsupportedVersion, "another version was not reported");
    for (const std::string& garbage : { std::string{}, std::string{ "kb1." }, std::string{ "kb1.." }, std::string{ "kb1.a.b" }, std::string{ "x" }, token.substr(0U, token.size() - 1U),
             token + ".x", token + "A", std::string(kMaxTokenBytes + 1U, 'a'), std::string{ "kb1.!!!!.@@@@" } }) {
        Require(VerifyToken(garbage, publicKey, kBase + 10).status != AuthStatus::Ok, "garbage verified: " + garbage.substr(0U, 20U));
    }
    Random random{ 0x9E3779B97F4A7C15ULL };
    const std::string alphabet = "kb1.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=+/ ";
    for (int run = 0; run < 3000; ++run) {
        std::string text = run % 2 == 0 ? "kb1." : "";
        const std::size_t length = random.Next() % 300U;
        for (std::size_t index = 0U; index < length; ++index) text.push_back(alphabet[random.Next() % alphabet.size()]);
        Require(VerifyToken(text, publicKey, kBase + 10).status != AuthStatus::Ok, "random text verified");
    }
}

// ------------------------------------------------------------------ revocation

void TestRevocationList() {
    kb::security::Ed25519SecretKey secret;
    kb::security::Ed25519PublicKey publicKey{};
    kb::security::Ed25519KeyPairFromSeed(MakeSeed(1U), secret, publicKey);
    kb::security::Ed25519SecretKey otherSecret;
    kb::security::Ed25519PublicKey otherPublic{};
    kb::security::Ed25519KeyPairFromSeed(MakeSeed(2U), otherSecret, otherPublic);

    AuthClaims claims = SampleClaims();
    RevocationList list;
    Require(!list.IsRevoked(claims) && list.Size() == 0U, "an empty list revoked a token");
    list.RevokeToken(claims.tokenId);
    Require(list.IsRevoked(claims), "a revoked token is not revoked");
    AuthClaims other = claims;
    other.tokenId = 78U;
    Require(!list.IsRevoked(other), "another token was revoked");
    list.RevokeSubject("grace", kBase + 100);
    AuthClaims grace = claims;
    grace.subject = "grace";
    grace.tokenId = 90U;
    grace.issuedAt = kBase + 100;
    Require(list.IsRevoked(grace), "a token issued at the limit of a subject revocation is not revoked");
    grace.issuedAt = kBase + 101;
    Require(!list.IsRevoked(grace), "a token issued after the subject revocation is revoked");
    list.RevokeSubject("grace", kBase + 50);
    grace.issuedAt = kBase + 100;
    Require(list.IsRevoked(grace), "a later revocation was lowered by an earlier one");

    const std::vector<std::uint8_t> bytes = list.Serialize(secret, kBase + 200);
    RevocationList loaded;
    std::int64_t published = 0;
    Require(RevocationList::Parse(bytes, publicKey, loaded, &published) && published == kBase + 200, "a signed list did not load");
    Require(loaded.IsRevoked(claims) && loaded.IsRevoked(grace) && loaded.Size() == list.Size(), "a loaded list differs");

    RevocationList untouched;
    untouched.RevokeToken(1234U);
    Require(!RevocationList::Parse(bytes, otherPublic, untouched), "a list signed by another key loaded");
    Require(untouched.Size() == 1U, "a refused list changed the list it was loaded into");
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        std::vector<std::uint8_t> changed = bytes;
        changed[index] ^= 0x01U;
        Require(!RevocationList::Parse(changed, publicKey, untouched), "a changed list loaded (byte " + std::to_string(index) + ")");
    }
    for (std::size_t size = 0U; size < bytes.size(); ++size) {
        Require(!RevocationList::Parse(std::span<const std::uint8_t>(bytes.data(), size), publicKey, untouched), "a truncated list loaded");
    }
    Require(untouched.Size() == 1U, "refused lists changed the list");
    RevocationList empty;
    Require(RevocationList::Parse(empty.Serialize(secret, kBase), publicKey, untouched) && untouched.Size() == 0U, "an empty signed list did not replace the list");
}

// ------------------------------------------------------------------ the provider

void TestProvider() {
    LocalAuthProvider provider(MakeSeed(3U), FastConfig());
    Require(!provider.AddAccount("", "longenough", kScopePlay), "an account without a name was added");
    Require(!provider.AddAccount("short", "1234567", kScopePlay), "a password of seven bytes was accepted");
    Require(provider.AddAccount("ada", "correct horse", kScopePlay | kScopeChat), "an account was not added");
    Require(!provider.AddAccount("ada", "another pass", kScopePlay), "a name was taken twice");
    Require(provider.AddAccount("grace", "battery staple", kScopePlay | kScopeModerate), "the second account was not added");

    const LoginResult wrong = provider.Login("ada", "wrong password", kBase);
    const LoginResult unknown = provider.Login("nobody", "correct horse", kBase);
    Require(!wrong.ok && !unknown.ok && wrong.error == unknown.error && wrong.token.empty(), "a wrong password and an unknown name answer differently");

    const LoginResult ok = provider.Login("ada", "correct horse", kBase);
    Require(ok.ok && ok.claims.subject == "ada" && ok.claims.scopes == (kScopePlay | kScopeChat) && ok.claims.expiresAt == kBase + 60, "a login did not give the account's claims");
    Require(VerifyToken(ok.token, provider.PublicKey(), kBase + 1).status == AuthStatus::Ok, "the provider's token does not verify under its public key");
    SessionKey keyOfHost{};
    Require(provider.Keys().LookupKey(ok.clientId, keyOfHost) && keyOfHost == ok.sessionKey, "the session key was not handed to the key table");
    const LoginResult again = provider.Login("ada", "correct horse", kBase + 1);
    Require(again.ok && again.claims.tokenId != ok.claims.tokenId && again.clientId == ok.clientId && again.sessionKey != ok.sessionKey, "a second login did not give a new token and key for the same client");
    const LoginResult other = provider.Login("grace", "battery staple", kBase);
    Require(other.ok && other.clientId != ok.clientId, "two accounts share a client id");

    // Lock-out: three wrong passwords, then even the right one waits.
    for (int attempt = 0; attempt < 3; ++attempt) Require(!provider.Login("grace", "nope nope nope", kBase + 10).ok, "a wrong password logged in");
    const LoginResult locked = provider.Login("grace", "battery staple", kBase + 11);
    Require(!locked.ok && locked.error.find("locked") != std::string::npos, "a locked account logged in");
    Require(provider.Login("grace", "battery staple", kBase + 10 + 30).ok, "the lock did not end");

    // Revocation and ban.
    provider.RevokeAccount("ada", kBase + 100);
    SessionKey gone{};
    Require(!provider.Keys().LookupKey(ok.clientId, gone), "the session key of a revoked account stayed");
    Require(provider.Revocations().IsRevoked(again.claims) && provider.Revocations().IsRevoked(ok.claims), "the tokens of a revoked account are not revoked");
    const LoginResult relogin = provider.Login("ada", "correct horse", kBase + 101);
    Require(relogin.ok && !provider.Revocations().IsRevoked(relogin.claims), "an account could not log in again after a revocation");
    provider.BanAccount("ada", kBase + 200);
    Require(!provider.Login("ada", "correct horse", kBase + 300).ok && provider.Revocations().IsRevoked(relogin.claims), "a banned account logged in or kept its tokens");
    RevocationList published;
    Require(RevocationList::Parse(provider.PublishRevocations(kBase + 300), provider.PublicKey(), published) && published.IsRevoked(relogin.claims), "the published list does not name the revoked tokens");
    provider.RevokeToken(other.claims.tokenId);
    Require(provider.Revocations().IsRevoked(other.claims), "a token was not revoked on its own");

    // The default stretching works too.
    LocalAuthProvider slow(MakeSeed(4U));
    Require(slow.AddAccount("lin", "a long enough pass", kScopePlay) && slow.Login("lin", "a long enough pass", kBase).ok && !slow.Login("lin", "a long enough pasS", kBase).ok, "default password stretching failed");
}

// ------------------------------------------------------------------ the gate

struct GateRig {
    explicit GateRig(AuthGateConfig gateConfig = {}, bool fillKey = true)
        : provider(MakeSeed(5U), FastConfig()), network() {
        hostPort = network.CreateTransport(Endpoint::Loopback(7000));
        host = std::make_unique<Host>(*hostPort, HostConfig{}, provider.Keys());
        if (fillKey) gateConfig.providerKey = provider.PublicKey();
        AuthGateHooks hooks;
        hooks.onAuthenticated = [this](std::uint64_t peer, const AuthClaims& claims) { authenticated.emplace_back(peer, claims.subject); };
        hooks.onLoginRefused = [this](std::uint64_t, AuthStatus status) { refused.push_back(status); };
        hooks.onCommandDenied = [this](std::uint64_t, std::uint32_t missing) { denied.push_back(missing); };
        hooks.onKicked = [this](std::uint64_t, AuthKickReason reason) { kicked.push_back(reason); };
        gate = std::make_unique<AuthGate>(*host, gateConfig, hooks);
        Require(provider.AddAccount("ada", "correct horse", kScopePlay | kScopeChat), "account");
        Require(provider.AddAccount("grace", "battery staple", kScopePlay | kScopeModerate), "account");
    }

    // A client for an account that has logged in with the provider; its transport is `port`.
    struct Player {
        std::unique_ptr<IDatagramTransport> port;
        std::unique_ptr<Client> client;
        LoginResult login;
        std::vector<NetEvent> events;
    };
    Player Connect(const std::string& name, const std::string& password, std::uint16_t port, std::uint64_t nowMilliseconds) {
        Player player;
        player.login = provider.Login(name, password, kBase + static_cast<std::int64_t>(nowMilliseconds / 1000U));
        Require(player.login.ok, "the provider refused " + name);
        player.port = network.CreateTransport(Endpoint::Loopback(port));
        player.client = std::make_unique<Client>(*player.port, ClientConfig{}, player.login.clientId, player.login.sessionKey, Endpoint::Loopback(7000), nowMilliseconds);
        return player;
    }

    void Step(std::uint64_t nowMilliseconds, std::vector<Player*> players) {
        network.Advance(nowMilliseconds);
        host->Update(nowMilliseconds);
        for (Player* player : players) player->client->Update(nowMilliseconds);
        gate->Update(nowMilliseconds, kBase + static_cast<std::int64_t>(nowMilliseconds / 1000U), commands);
        for (Player* player : players) {
            NetEvent event;
            while (player->client->PollEvent(event)) player->events.push_back(std::move(event));
        }
    }
    std::uint64_t Run(std::uint64_t from, std::uint64_t until, std::vector<Player*> players, const std::function<bool()>& done = {}) {
        std::uint64_t now = from;
        for (; now < until; now += 10U) {
            Step(now, players);
            if (done && done()) break;
        }
        return now;
    }
    [[nodiscard]] static bool Got(const Player& player, std::uint8_t type, std::uint32_t value) {
        return std::ranges::any_of(player.events, [&](const NetEvent& event) {
            if (event.type != NetEventType::Message || event.message.data.size() != 5U || event.message.data[0] != type) return false;
            const auto& d = event.message.data;
            return (static_cast<std::uint32_t>(d[1]) | (static_cast<std::uint32_t>(d[2]) << 8U) | (static_cast<std::uint32_t>(d[3]) << 16U) | (static_cast<std::uint32_t>(d[4]) << 24U)) == value;
        });
    }
    [[nodiscard]] static bool Disconnected(const Player& player) {
        return std::ranges::any_of(player.events, [](const NetEvent& event) { return event.type == NetEventType::Disconnected; });
    }
    [[nodiscard]] static std::vector<std::uint8_t> Text(const std::string& text) { return { text.begin(), text.end() }; }

    LocalAuthProvider provider;
    LoopbackNetwork network;
    std::unique_ptr<IDatagramTransport> hostPort;
    std::unique_ptr<Host> host;
    std::unique_ptr<AuthGate> gate;
    std::vector<NetEvent> commands;
    std::vector<std::pair<std::uint64_t, std::string>> authenticated;
    std::vector<AuthStatus> refused;
    std::vector<std::uint32_t> denied;
    std::vector<AuthKickReason> kicked;
};

void TestGateLogin() {
    GateRig rig;
    auto ada = rig.Connect("ada", "correct horse", 7001U, 0U);
    std::uint64_t now = rig.Run(0U, 2000U, { &ada }, [&] { return ada.client->State() == ClientState::Connected; });
    Require(ada.client->State() == ClientState::Connected, "the client did not connect");

    // Before logging in, a command is refused and answered.
    Require(ada.client->Send(0U, MakeCommandMessage(GateRig::Text("move"))) == SendResult::Queued, "send");
    now = rig.Run(now, now + 300U, { &ada });
    Require(rig.commands.empty() && GateRig::Got(ada, kAuthMessageDenied, 0U), "a command before login passed or was not answered");

    // A good token logs in, and then commands pass with their payload.
    Require(ada.client->Send(0U, MakeLoginMessage(ada.login.token)) == SendResult::Queued, "send");
    now = rig.Run(now, now + 500U, { &ada }, [&] { return GateRig::Got(ada, kAuthMessageAccepted, kScopePlay | kScopeChat); });
    Require(rig.gate->AuthenticatedCount() == 1U && GateRig::Got(ada, kAuthMessageAccepted, kScopePlay | kScopeChat), "the login was not accepted");
    Require(rig.authenticated.size() == 1U && rig.authenticated[0].second == "ada" && rig.gate->Claims(ada.login.clientId) != nullptr, "the hook or the claims are missing");
    Require(ada.client->Send(0U, MakeCommandMessage(GateRig::Text("move left"))) == SendResult::Queued, "send");
    now = rig.Run(now, now + 300U, { &ada }, [&] { return !rig.commands.empty(); });
    Require(rig.commands.size() == 1U && rig.commands[0].peer == ada.login.clientId && rig.commands[0].message.data == GateRig::Text("move left"), "a command of a logged-in client did not arrive intact");

    // Scopes: the server asks before it acts.
    Require(rig.gate->Authorize(ada.login.clientId, kScopePlay | kScopeChat), "a held scope was refused");
    Require(!rig.gate->Authorize(ada.login.clientId, kScopeModerate), "a missing scope was allowed");
    now = rig.Run(now, now + 300U, { &ada });
    Require(rig.denied.size() == 1U && rig.denied[0] == kScopeModerate && GateRig::Got(ada, kAuthMessageDenied, kScopeModerate), "the denial was not reported");
    Require(rig.gate->GetStats().commandsPassed == 1U && rig.gate->GetStats().loginsAccepted == 1U, "the counters are wrong");
    Require(!rig.gate->Authorize(999U, kScopePlay), "an unknown peer was allowed");
}

void TestGateRefusals() {
    GateRig rig;
    // A token from another provider, a token of another client, garbage, and a repeat offender.
    LocalAuthProvider rogue(MakeSeed(9U), FastConfig());
    Require(rogue.AddAccount("ada", "correct horse", kScopePlay | kScopeAdmin), "account");
    const LoginResult forged = rogue.Login("ada", "correct horse", kBase);
    Require(forged.ok, "rogue login");

    auto ada = rig.Connect("ada", "correct horse", 7001U, 0U);
    auto grace = rig.Connect("grace", "battery staple", 7002U, 0U);
    std::uint64_t now = rig.Run(0U, 2000U, { &ada, &grace }, [&] { return ada.client->State() == ClientState::Connected && grace.client->State() == ClientState::Connected; });
    Require(ada.client->State() == ClientState::Connected && grace.client->State() == ClientState::Connected, "the clients did not connect");

    Require(ada.client->Send(0U, MakeLoginMessage(forged.token)) == SendResult::Queued, "send");
    now = rig.Run(now, now + 300U, { &ada, &grace });
    Require(GateRig::Got(ada, kAuthMessageRefused, static_cast<std::uint32_t>(AuthStatus::BadSignature)) && rig.gate->AuthenticatedCount() == 0U, "a forged token was accepted");
    Require(grace.client->Send(0U, MakeLoginMessage(ada.login.token)) == SendResult::Queued, "send");
    now = rig.Run(now, now + 300U, { &ada, &grace });
    Require(GateRig::Got(grace, kAuthMessageRefused, static_cast<std::uint32_t>(AuthStatus::WrongClient)) && rig.gate->AuthenticatedCount() == 0U, "a token of another client was accepted");
    Require(rig.refused.size() == 2U, "the refusals were not reported to the hook");

    // Three violations end a connection: each refused login counts, and so does a message of an unknown kind.
    Require(ada.client->Send(0U, MakeLoginMessage("kb1.not-a-token")) == SendResult::Queued && ada.client->Send(0U, MakeLoginMessage("kb1.not-a-token")) == SendResult::Queued, "send");
    Require(grace.client->Send(0U, std::vector<std::uint8_t>{ 0x55U, 1U, 2U }) == SendResult::Queued && grace.client->Send(0U, std::vector<std::uint8_t>{ 0x55U, 1U, 2U }) == SendResult::Queued, "send");
    now = rig.Run(now, now + 1000U, { &ada, &grace }, [&] { return GateRig::Disconnected(ada) && GateRig::Disconnected(grace); });
    Require(GateRig::Disconnected(ada) && GateRig::Disconnected(grace), "repeat offenders were not disconnected");
    Require(GateRig::Got(ada, kAuthMessageKicked, static_cast<std::uint32_t>(AuthKickReason::TooManyViolations)), "the kick was not explained");
    Require(rig.kicked.size() == 2U && rig.kicked[0] == AuthKickReason::TooManyViolations, "the kicks were not reported");
    Require(rig.host->ClientCount() == 0U, "the host kept the clients");
}

void TestGateCommandLimits() {
    AuthGateConfig config;
    config.maxCommandBytes = 16U;
    config.maxCommandsPerUpdate = 3U;
    config.maxViolations = 100U;
    GateRig rig(config);
    auto ada = rig.Connect("ada", "correct horse", 7001U, 0U);
    std::uint64_t now = rig.Run(0U, 2000U, { &ada }, [&] { return ada.client->State() == ClientState::Connected; });
    Require(ada.client->Send(0U, MakeLoginMessage(ada.login.token)) == SendResult::Queued, "send");
    now = rig.Run(now, now + 500U, { &ada }, [&] { return rig.gate->AuthenticatedCount() == 1U; });
    Require(rig.gate->AuthenticatedCount() == 1U, "login");
    Require(ada.client->Send(0U, MakeCommandMessage(GateRig::Text("fits"))) == SendResult::Queued, "send");
    Require(ada.client->Send(0U, MakeCommandMessage(GateRig::Text("this command is far too long"))) == SendResult::Queued, "send");
    now = rig.Run(now, now + 500U, { &ada });
    Require(rig.commands.size() == 1U && rig.gate->GetStats().commandsRefused == 1U, "an oversized command passed or a good one was lost");
    // A burst over the per-update limit: the rest are refused, not queued.
    rig.commands.clear();
    const std::uint64_t refusedBefore = rig.gate->GetStats().commandsRefused;
    for (int index = 0; index < 12; ++index) Require(ada.client->Send(0U, MakeCommandMessage(GateRig::Text("tick"))) == SendResult::Queued, "send");
    now = rig.Run(now, now + 500U, { &ada });
    Require(rig.commands.size() + (rig.gate->GetStats().commandsRefused - refusedBefore) == 12U && rig.commands.size() >= 3U, "commands were lost without being counted");
}

void TestGateExpiryAndRevocation() {
    GateRig rig;
    auto ada = rig.Connect("ada", "correct horse", 7001U, 0U);
    auto grace = rig.Connect("grace", "battery staple", 7002U, 0U);
    std::uint64_t now = rig.Run(0U, 2000U, { &ada, &grace }, [&] { return ada.client->State() == ClientState::Connected && grace.client->State() == ClientState::Connected; });
    Require(ada.client->Send(0U, MakeLoginMessage(ada.login.token)) == SendResult::Queued && grace.client->Send(0U, MakeLoginMessage(grace.login.token)) == SendResult::Queued, "send");
    now = rig.Run(now, now + 1000U, { &ada, &grace }, [&] { return rig.gate->AuthenticatedCount() == 2U; });
    Require(rig.gate->AuthenticatedCount() == 2U, "login");

    // Revocation: the provider withdraws grace; nothing happens until the server loads the list.
    rig.provider.RevokeAccount("grace", kBase + static_cast<std::int64_t>(now / 1000U));
    now = rig.Run(now, now + 500U, { &ada, &grace });
    Require(rig.gate->AuthenticatedCount() == 2U, "a peer was dropped before the list was loaded");
    const std::vector<std::uint8_t> list = rig.provider.PublishRevocations(kBase + static_cast<std::int64_t>(now / 1000U));
    Require(rig.gate->UpdateRevocations(list), "the server refused the provider's list");
    Require(!rig.gate->UpdateRevocations(rig.provider.PublishRevocations(kBase - 5)), "an older list replaced a newer one");
    std::vector<std::uint8_t> tampered = list;
    tampered[tampered.size() / 2U] ^= 0x10U;
    Require(!rig.gate->UpdateRevocations(tampered), "a changed list was loaded");
    LocalAuthProvider rogue(MakeSeed(11U), FastConfig());
    Require(!rig.gate->UpdateRevocations(rogue.PublishRevocations(kBase + 100000)), "a list signed by another key was loaded");
    now = rig.Run(now, now + 500U, { &ada, &grace }, [&] { return GateRig::Disconnected(grace); });
    Require(GateRig::Disconnected(grace) && GateRig::Got(grace, kAuthMessageKicked, static_cast<std::uint32_t>(AuthKickReason::Revoked)), "a revoked peer was not disconnected and told why");
    Require(rig.gate->AuthenticatedCount() == 1U && !GateRig::Disconnected(ada), "the revocation of one peer touched another");
    Require(rig.kicked.size() == 1U && rig.kicked[0] == AuthKickReason::Revoked, "the kick hook did not run");

    // The revoked account cannot connect again: its session key is gone from the provider's table.
    SessionKey key{};
    Require(!rig.provider.Keys().LookupKey(grace.login.clientId, key), "the revoked account still has a session key");

    // Expiry: the token lives sixty seconds; ada is dropped when it ends, and not before.
    now = rig.Run(now, 59'000U, { &ada });
    Require(!GateRig::Disconnected(ada) && rig.gate->AuthenticatedCount() == 1U, "a peer was dropped before its token expired");
    now = rig.Run(now, 62'000U, { &ada }, [&] { return GateRig::Disconnected(ada); });
    Require(GateRig::Disconnected(ada) && GateRig::Got(ada, kAuthMessageKicked, static_cast<std::uint32_t>(AuthKickReason::Expired)), "an expired peer was not disconnected and told why");
    Require(rig.kicked.back() == AuthKickReason::Expired && rig.gate->AuthenticatedCount() == 0U, "the expiry was not reported");
}

void TestGateLoginTimeout() {
    AuthGateConfig config;
    config.loginTimeoutMilliseconds = 1500U;
    GateRig rig(config);
    auto ada = rig.Connect("ada", "correct horse", 7001U, 0U);
    std::uint64_t now = rig.Run(0U, 1000U, { &ada }, [&] { return ada.client->State() == ClientState::Connected; });
    Require(ada.client->State() == ClientState::Connected, "connect");
    now = rig.Run(now, 5000U, { &ada }, [&] { return GateRig::Disconnected(ada); });
    Require(GateRig::Disconnected(ada) && !rig.kicked.empty() && rig.kicked[0] == AuthKickReason::LoginTimeout, "a client that never logged in was kept");
    Require(now < 4000U, "the login timeout took too long");
}

void TestGateRealUdp() {
    std::string error;
    auto hostSocket = CreateUdpTransport(Endpoint::Loopback(0U), error);
    if (hostSocket == nullptr) {
        std::printf("  skipped: no UDP socket (%s)\n", error.c_str());
        return;
    }
    auto clientSocket = CreateUdpTransport(Endpoint::Loopback(0U), error);
    Require(clientSocket != nullptr, "the second socket could not be created: " + error);

    LocalAuthProvider provider(MakeSeed(6U), FastConfig());
    Require(provider.AddAccount("ada", "correct horse", kScopePlay), "account");
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };
    // The wall clock of the test: a base plus the time it has run, and a jump for the expiry.
    std::int64_t skewSeconds = 0;
    const auto wall = [&] { return kBase + static_cast<std::int64_t>(elapsed() / 1000U) + skewSeconds; };

    const LoginResult login = provider.Login("ada", "correct horse", wall());
    Require(login.ok, "login");
    Host host(*hostSocket, HostConfig{}, provider.Keys());
    AuthGateConfig config;
    config.providerKey = provider.PublicKey();
    AuthGate gate(host, config);
    Client client(*clientSocket, ClientConfig{}, login.clientId, login.sessionKey, Endpoint::Loopback(hostSocket->LocalEndpoint().port), elapsed());

    std::vector<NetEvent> commands;
    bool loginSent = false;
    bool commandSent = false;
    bool kickedSeen = false;
    bool acceptedSeen = false;
    bool jumped = false;
    while (elapsed() < 8000U && !kickedSeen) {
        const std::uint64_t now = elapsed();
        host.Update(now);
        client.Update(now);
        gate.Update(now, wall(), commands);
        NetEvent event;
        while (client.PollEvent(event)) {
            if (event.type == NetEventType::Message && !event.message.data.empty()) {
                acceptedSeen = acceptedSeen || event.message.data[0] == kAuthMessageAccepted;
                kickedSeen = kickedSeen || event.message.data[0] == kAuthMessageKicked;
            }
        }
        if (!loginSent && client.State() == ClientState::Connected) loginSent = client.Send(0U, MakeLoginMessage(login.token)) == SendResult::Queued;
        if (acceptedSeen && !commandSent) commandSent = client.Send(0U, MakeCommandMessage(std::vector<std::uint8_t>{ 1U, 2U, 3U })) == SendResult::Queued;
        if (!commands.empty() && !jumped) {
            skewSeconds = 120; // the token lives sixty seconds
            jumped = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(acceptedSeen && commands.size() == 1U && commands[0].message.data == std::vector<std::uint8_t>({ 1U, 2U, 3U }), "the login and a command did not make the round trip over UDP");
    Require(kickedSeen && gate.AuthenticatedCount() == 0U, "an expired token did not end the session over UDP");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "token-format", &TestTokenFormat },
        { "revocation-list", &TestRevocationList },
        { "provider", &TestProvider },
        { "gate-login", &TestGateLogin },
        { "gate-refusals", &TestGateRefusals },
        { "gate-command-limits", &TestGateCommandLimits },
        { "gate-expiry-and-revocation", &TestGateExpiryAndRevocation },
        { "gate-login-timeout", &TestGateLoginTimeout },
        { "gate-real-udp", &TestGateRealUdp },
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        std::printf("START %s\n", name);
        std::fflush(stdout);
        try {
            run();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            std::printf("FAIL %s: %s\n", name, error.what());
            ++failures;
        }
        std::fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}
