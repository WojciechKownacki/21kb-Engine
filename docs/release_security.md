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
later Authenticode signature covers it and replacing it breaks that signature; the asset
`kb_trust_anchor.bin` inside the signed APK on Android. A player that carries an anchor runs in
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

## Save games

Save files (schema 3) carry an HMAC-SHA512 over their header and payload next to the FNV-1a
checksum of earlier schemas. A save whose checksum fails is `IntegrityMismatch` (damaged on
disk); a save whose checksum passes but whose HMAC does not is `Tampered` (edited outside the
game, or written by another installation). Neither is loaded; `Save.Read` reports the status.

The key is HKDF-SHA512 over two secrets:

- the **per-game secret** in the trust anchor, derived from the release signing key and the
  product id, so every release signed with the same key reads the same saves;
- a random **per-installation secret** created on first start in per-user storage
  (`%LOCALAPPDATA%\21kb\<product>\installation.secret`; the app's internal storage on Android).

Development players and the editor use a fixed development key: hand edits are caught, but
anyone can forge a save. Both secrets live on the player's machine, so the HMAC makes editing a
save deliberate reverse-engineering work, not something a hex editor or a copied save does by
accident; it cannot stop someone who extracts both. Because saves are bound to an installation,
a save copied to another machine (or kept after deleting the per-user state) reads as `Tampered`;
a game that syncs saves between machines should sync them through its own service.

Saves written before schema 3 (unauthenticated schema 1 and 2) load once, keep every value, and
are rewritten authenticated in place (`migrated` in the load result). An unauthenticated file
cannot be told apart from a forged one, so a game whose first release already wrote schema 3
should set `SaveGameIntegrity::acceptUnauthenticatedLegacySaves` to `false`.

## Platform limits

- **Windows**: full packaged mode. The trust anchor is a PE resource; sign the executable with
  Authenticode after packaging embeds it so the anchor cannot be swapped.
- **Android**: the anchor is an APK asset, protected by the APK signature. Encryption is not
  offered (the Gradle build validates the pack on the host without the content key).
- **Linux and the browser**: an ELF binary and a WebAssembly module carry no resource section
  packaging can write after the build, so these players have no trust anchor and run like
  development players: a sealed pack is still checked for integrity against the key it names,
  but nothing stops a replaced pack signed with another key. Distribute them through a channel
  that signs the whole download.
