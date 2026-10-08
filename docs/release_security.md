# Release security

How a packaged 21kb game proves that what it loads is what its publisher shipped, and where the
keys behind that live. All cryptography comes from Monocypher through
`sources/engine/include/engine/security/Crypto.hpp`; nothing else in the engine implements or
calls a primitive directly.

## Keys

| Key | What it is | Where it lives | Ships? |
| --- | --- | --- | --- |
| Release signing key | Ed25519 key pair, `kb_cli keys generate` | A key file outside every project | Public half only |
| Pack content key | XChaCha20-Poly1305 key, one per release | The packaging job directory, deleted with it | Inside the player |
| Trust anchor | Public key, product id, content key | Embedded in the player | Yes |

- `kb_cli keys generate --out <file>` refuses to write inside a project or a version-control
  working tree. Keep the file private and backed up: every update of a game must be signed with
  the same key, or the shipped player refuses it.
- Without `--signing-key`, `scripts/package_game.py` uses `<LOCALAPPDATA>\21kb\ReleaseKeys\<product>.kbkey`
  (`$XDG_DATA_HOME/21kb/release-keys` elsewhere, `KB_RELEASE_KEY_ROOT` overrides) and creates it
  on first use with a warning. The editor passes the `ReleaseSigningKey` of its local Build Game
  settings, which live outside the project.
- `--signing-broker <program>` keeps the key away from packaging altogether. For every operation
  that needs the key, packaging writes a request
  `{"schema":1,"session":…,"kind":"kbReleaseSigning","kbCli":…,"key":…|null,"arguments":[…]}`,
  runs `<program> --request <file> --response <file>`, and expects
  `{"schema":1,"session":…,"succeeded":true}`. The broker runs `kbCli` with the arguments plus
  `--key <its key file>`, so an HSM front end or a passphrase prompt fits behind it.
- The product id (`--product-id`, default `<publisher>.<product name>` reduced to
  `[A-Za-z0-9._-]`) names the game in the trust anchor and, later, in per-user state.

## The trust anchor and packaged mode

Packaging embeds the trust anchor into the player before anything else touches the executable:
an `RT_RCDATA` resource (id 2101) written by `scripts/windows_pe_resources.py` on Windows, so a
later Authenticode signature covers it and replacing it breaks that signature; the 1024-byte
`.kb_trust_anchor` section every Linux player is linked with, filled in place by
`scripts/elf_trust_anchor.py` on the Linux build machine before the player's first-frame proof
and build receipt (the host then checks the returned player carries exactly this release's
anchor); the asset `kb_trust_anchor.bin` inside the signed APK on Android. A player that carries an anchor runs in
**packaged mode** and refuses unsigned or foreign content, including a loose project passed with
`--project`. A player without one (the editor, a
development `kb_game`) accepts it and says so. A damaged anchor is an error, never a fallback to
development behaviour.

## Asset packs

`kb_cli pack sign --key <key> [--content-key <file>] <pack>` appends a seal to the pack: an Ed25519
signature over the raw header, the artifact index, the fragment index and one SHA-512 per block.
Packaging seals the cooked pack right after the cook (`_sign_pack`) and verifies it with
`kb_cli pack verify --anchor <anchor> <pack>`, which reads every block.

At mount the reader checks the signature before it decodes a single index entry; each block's
SHA-512 is checked when that block is read. A pack is never hashed as a whole at startup and no
block is hashed twice. In packaged mode a pack that is unsigned (`Unsigned`), signed by another
key (`UntrustedSigner`), modified anywhere in its catalogue or seal (`SignatureInvalid`,
`SealCorrupt`) is refused at mount with a message naming the reason; a modified block is refused
when it is read (`PayloadCorrupt`).

`--encrypt-pack` (Windows packages) additionally encrypts every block in place with
XChaCha20-Poly1305 under a fresh content key; the nonce is the seal's random salt followed by the
block offset and the tag sits in the block's seal entry, so the layout and every offset are
unchanged. The index stays readable. The content key has to ship inside the player to be usable,
so encryption keeps content away from ordinary extraction tools, not from a determined attacker;
the signature, not the encryption, is what makes tampering detectable.

## Release manifest

Once every file of a Windows release is final, packaging runs
`kb_cli release sign --dir <stage> --product <id> --content-version <version> --release <n>` and
then `kb_cli release verify <stage>`, which takes the release key from the trust anchor inside the
staged player. `release.kbmanifest` lists every file with its size and SHA-512, records for each
pack the digest its seal signs, and is signed with the release key. **Authenticode signing has to
happen before this step**: it changes the executable's bytes.

The release number (`--release-number`, default: the packaging time in seconds) only grows. At
startup a packaged player:

1. verifies the manifest against its trust anchor and product id;
2. refuses an unlisted executable, native module or pack anywhere in its directory (a planted
   `version.dll` is named in the error), and a listed one that is missing or has another size;
3. hashes its own executable against the manifest;
4. binds the mounted pack to the release through the pack's seal digest, so the pack is not
   hashed a second time;
5. applies anti-rollback when the release asks for it;
6. installs the verified release for native module loading.

Other files (licenses, notices) are checked by `kb_cli release verify`, which hashes everything
and reports files that were added later and are outside the critical set (the packaging receipt,
for example) without failing on them.

**Anti-rollback** is off by default. With `--anti-rollback` the player keeps the highest release
number it has run in `%LOCALAPPDATA%\21kb\<product>\release.state` and refuses an older release.
It keeps a known-bad build from being reinstalled over a fixed one, and it also blocks a
deliberate downgrade; deleting the per-user state resets it, so it is a policy aid, not a
guarantee.

## Native modules

Every native module (engine plugins and native script plugins) is loaded by
`EngineModuleLoader`. In a packaged player that installed its verified release, a module loads
only when the release manifest lists it (by its path inside the release) and the SHA-512 of the
exact bytes about to be mapped matches: the shadow copy in `%TEMP%` (or the module itself) is
opened without write or delete sharing, hashed through that handle, and kept locked until
`LoadLibraryW` has mapped it, so it cannot be swapped between the check and the load. An
unlisted, modified or out-of-release module is an error and the player does not start. The
editor and development players load modules with a warning (`module warning:` in the player's
log) that they are not verified. On Linux the module is hashed through an open descriptor but
loaded by path; packaged Linux players are monolithic and load no modules.

## Save games

Save files (schema 3) carry an HMAC-SHA512 over their header and payload next to the FNV-1a
checksum of earlier schemas. A save whose checksum fails is `IntegrityMismatch` (damaged on
disk); a save whose checksum passes but whose HMAC does not is `Tampered` (edited outside the
game, or written by another game). Neither is loaded; `Save.Read` reports the status.

A packaged player's key is HKDF-SHA512 over the **per-game secret** in its trust anchor, derived
from the release signing key and the product id, so every release signed with the same key reads
the same saves, on any of the player's machines: saves move through cloud saves, between
computers and across reinstalls. A game that wants saves bound to one installation can add a
random per-installation secret (`LoadOrCreateInstallationSecret`, passed to
`DeriveSaveGameIntegrity`); a save copied from another machine then reads as `Tampered`.

Development players and the editor use a fixed development key: hand edits are caught, but
anyone can forge a save. The per-game secret lives in the player's copy of the game, so the HMAC
makes editing a save deliberate reverse-engineering work, not something a hex editor does by
accident; it cannot stop someone who extracts the secret.

Saves written before schema 3 (unauthenticated schema 1 and 2) load once, keep every value, and
are rewritten authenticated in place (`migrated` in the load result). An unauthenticated file
cannot be told apart from a forged one, so a game whose first release already wrote schema 3
should set `SaveGameIntegrity::acceptUnauthenticatedLegacySaves` to `false`.

## Platform limits

- **Windows**: full packaged mode. The trust anchor is a PE resource; sign the executable with
  Authenticode after packaging embeds it so the anchor cannot be swapped.
- **Android**: the anchor is an APK asset, protected by the APK signature. Encryption is not
  offered (the Gradle build validates the pack on the host without the content key).
- **Linux**: the trust anchor is the player's `.kb_trust_anchor` ELF section, so a Linux player
  runs in packaged mode: it refuses a pack its release key did not sign and authenticates saves
  with the per-game secret. An ELF file carries no code signature, so anyone who can write the
  player can also replace its anchor, and Linux packages get no release manifest: the startup
  check of the installed file set is Windows only. Distribute through a channel that signs the
  whole download.
- **The browser**: a WebAssembly module carries no section packaging can write after the
  build, so a web player has no trust anchor and runs like a development player: a sealed pack
  is still checked for integrity against the key it names, but nothing stops a replaced pack
  signed with another key. It gets no release manifest and no save secret either.
- **DLLs the operating system loads before `main`** (search-order hijacking of system DLL names)
  run before any check of the player's. The startup check refuses to continue with one present,
  but cannot undo what its initialiser already did; install games into directories users cannot
  write to.
