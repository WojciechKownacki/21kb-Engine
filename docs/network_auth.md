# Login, tokens and permissions for a game server (kb_net)

`kb_net` has two layers below this one: the datagram **session** (every packet sealed and replay-checked) and the **handshake** that proves a
client knows the 32 byte session key the host has on file for its client id (`kb/net/Host.hpp`). This layer, `kb/net/Auth.hpp`, answers the
questions above them: how a player gets that key, who a connected client is, what it may do, and how an operator takes access away.

It is authentication and authorisation for the commands of a server that already validates them (`NetworkSecurity.hpp`). It is not an
anti-cheat and does not make a client trustworthy; it gives the server a verified identity and a set of permissions to decide with.

## The model

```
 player ──(name, password)──► login provider ──► token + session key ──► player
                                   │  (signs with Ed25519)
                                   └─ registers the session key with the game server's key table
 player ──► game server:  connect (handshake with the session key) ─► first message: the token
 game server (AuthGate):  verify signature, time, revocation, client id ─► accept / refuse
```

- A **token** is `kb1.<base64url payload>.<base64url signature>`: token id, client id, subject (the account name), issue and expiry time
  in seconds of the provider's wall clock, and a 32 bit scope mask. The Ed25519 signature covers a fixed domain string and the payload,
  so a game server needs only the provider's **public key** and never sees a password. Tokens live at most 30 days; the sample
  provider issues 15 minutes by default (`LocalAuthConfig::tokenLifetimeSeconds`).
- **Scopes** are bits a game defines (`kScopePlay`, `kScopeChat`, `kScopeModerate`, `kScopeAdmin` are the conventions the tests use).
  The server asks `AuthGate::Authorize(peer, scopes)` before it acts; a missing scope is answered to the client, reported to a hook, and
  counts as a violation.
- **Revocation.** The provider keeps a `RevocationList` (single token ids, and "every token of this subject issued up to a time"),
  signs a snapshot with `PublishRevocations` and the server loads it with `AuthGate::UpdateRevocations`. A list signed by another key,
  changed after signing, or older than the one loaded is refused. The gate disconnects the peers the list names on its next update.
  `RevokeAccount` also removes the account's session key, so it cannot connect again; `BanAccount` also refuses its logins.
- **Expiry.** The gate disconnects a peer when its token expires. A client that never sends a valid token is dropped after
  `loginTimeoutMilliseconds`.

## The server side

```cpp
kb::net::Host host(transport, hostConfig, provider.Keys());      // the provider's key table (or your own IKeyProvider)
kb::net::AuthGateConfig config; config.providerKey = providerPublicKey;
kb::net::AuthGate gate(host, config, hooks);                      // hooks: onAuthenticated, onLoginRefused, onCommandDenied, onKicked
// every frame:
host.Update(nowMilliseconds);
std::vector<kb::net::NetEvent> commands;
gate.Update(nowMilliseconds, nowUnixSeconds, commands);          // commands: only those of logged-in peers
for (auto& command : commands) if (gate.Authorize(command.peer, kb::net::kScopePlay)) Handle(command);
```

The gate reads the Host's events, so the application reads commands from the gate instead. Messages are framed with one leading byte:
`0xA1` login (the token), `0xA2` command (the rest is the command). The server answers with `0xB1` accepted (the scopes), `0xB2` refused
(the `AuthStatus`), `0xB3` denied (the missing scopes; 0 when not logged in) and `0xB4` kicked (the `AuthKickReason`), each followed by a
4 byte little endian value. `MakeLoginMessage` and `MakeCommandMessage` build the client's messages.

What the gate refuses, and what happens next: a command before login, a command over `maxCommandBytes`, more than `maxCommandsPerUpdate`
commands from one peer in one update, a message of an unknown kind, a forged or expired or revoked token, and a token for another client
id. Each is a **violation**; `maxViolations` of them (default 3) disconnect the peer. The kick notice is sent first and the connection
is closed on the following update.

## The sample provider

`LocalAuthProvider` is a real provider for tests, tools and small deployments: accounts with a random salt and a PBKDF2-HMAC-SHA512
verifier (20 000 rounds by default), a lock-out after `maxFailedLogins` wrong passwords in a row, a fresh random session key per login
(it replaces the account's previous one in the key table), signed tokens, revocation and ban. An unknown name
and a wrong password cost the same and answer the same. It keeps accounts in memory only and is not a replacement for an account
database; a game with real players puts its own account system behind the same token format. The cryptography is the engine's one door
(`engine/security/Crypto.hpp`, Monocypher); nothing here implements a primitive.

## Dedicated server

Pass `--auth-provider-key provider.pub` to require provider tokens in
`kb_dedicated_server`. The file contains the Ed25519 public key as 64 hex digits.
The existing `--keys` file supplies transport session keys; signing secrets stay
with the provider. Without the provider-key option, existing key-authenticated
transport sessions keep their previous behavior.

With token authorization enabled, replication and `OnNetConnected` wait for a
valid token with `kScopePlay`. Commands pass through the gate before RPC and
replication validation. Expiry, revocation and transport disconnect remove the
peer from replication and notify scripts. Status JSON includes an `authorization`
object with accepted/refused logins, refused commands and removed sessions.

Use `--auth-revocations revocations.bin` to load a signed snapshot at startup and
reload it once per second. Snapshots are limited to 1 MiB. An invalid initial
snapshot prevents startup; an invalid replacement retains the last valid list
and reports an error. Publish replacements atomically. Fetching these files from
an external provider remains the deployment's responsibility.

Clients set `ClientConfig::authorizationRequired`, call `Client::Authenticate`
after the transport connects, and wait for `kAuthMessageAccepted` with the required
scope before starting replication. `Client::Send` then frames application commands
for the gate. Server replies retain their existing message formats.

## Limits

- Revocation reaches a server when the server loads the new list; the delay is how often the game fetches it.
- Server and provider clocks must agree to within the allowed skew (30 seconds by default) for issue times; expiry is exact.
- The session key is long-lived per login; there is no forward secrecy (see `Host.hpp`).
- Handing the session key and token to the player, and the server's fetch of the revocation list, are the game's transport (HTTPS to
  its own service, for example); this layer defines the data and the checks, not that channel.
- An external identity service (a platform's login) is not covered: it would be a provider that issues these tokens.

## Tests

`kb_net_auth_tests`: token format and tampering (every single character changed, garbage and random text, every claim limit, time window
and skew), the revocation list (signature, truncation, bit flips, ordering), the provider (wrong password and unknown name look alike,
lock-out, ban, revocation, session keys), and the gate over the simulated network and over real UDP sockets (login, commands and
scopes, forged and misdirected tokens, repeat offenders, command limits, revocation and expiry disconnects, login timeout).
