# Content streaming and packaging

How a packaged 21kb game keeps its textures and meshes within a GPU memory budget, how its
content is laid out on disk across several packs, and how a release is patched without giving up
the signing described in [release_security.md](release_security.md).

The pieces, bottom up:

| Layer | Code |
| --- | --- |
| Pack format, block compression | `engine/assets/bake/AssetPack.hpp`, `AssetPackWriter.hpp`, `AssetPackReader.hpp`, `src/private/assets/bake/AssetPackCompression.hpp` |
| Pack sets (base, chunks, patches) | `engine/assets/bake/AssetPackSet.hpp`, `RuntimeAssetPack.hpp` |
| Pack tools | `engine/assets/bake/AssetPackTools.hpp`, `kb_cli pack …` |
| Background loading (reads, asset loads, texture decodes) | `engine/assets/streaming/BackgroundLoadService.hpp`, `PackBlockStream.hpp` |
| Residency under a budget | `engine/assets/streaming/StreamingResidency.hpp` |
| Texture and mesh streaming | `kb/render/runtime/RuntimeContentStreamer.hpp` and the texture and mesh bakers and loaders |
| Packaging | `scripts/package_game.py` |

## Pack layout

A `.kbpack` is a 256-byte fixed header, the artifact index, an optional fragment index and then
the blocks, each at an offset aligned to the target profile's block alignment (256 bytes). The
index sits at the front so one range request yields the whole catalogue. Every offset and size is
64-bit.

Format 3 added to the header the pack's place in a pack set: its **role** (base, chunk or patch),
a **label**, a **patch level** and the **base identity** -- the catalogue identity (a digest of the
header, artifact index and fragment index) of the base pack a chunk or patch was cooked against.
A format-2 pack still mounts, as a base.

Size ceilings:

| Limit | Value | Why |
| --- | --- | --- |
| One block | 128 MiB (`kMaxAssetPackBlockBytes`) | One range request fits a wasm32 heap; one block binds as one storage buffer |
| A pack read by ranges | 1 TiB (`kMaxAssetPackBytes`) | Every desktop and console mount reads by ranges |
| A pack read whole | 1.5 GiB (`kMaxWholeFileAssetPackBytes`) | The read-the-file fallback and `MountMemory` stay budgeted |

The engine content-streaming suite writes a sparse pack whose last block lies past 4 GiB and
reads it back through the ranged reader (`BlocksBeyondFourGigabytesAreAddressed`).

## Block compression

Compression is per block, never per file: a byte range of a file that a host compressed as a
whole cannot be addressed, so the container is stored and served uncompressed and each block
records how it is stored.

- A compressed block is one zstd frame (zstd 1.5.7, fetched by CMake with a pinned hash) that
  declares its content size. It decodes on its own, so random access survives compression.
- The writer compresses a block only when that saves at least 1/32 of it, and never a block
  under 64 bytes or a `Mapped` block (a mapping hands out bytes as they lie in the file). Every
  other block is stored as written.
- The level is 1 to 19, default 9. `kb_cooker --pack-compression-level <0-19>` and
  `package_game.py --pack-compression-level` choose it; 0 stores every block as baked.
- A reader refuses a frame that does not declare exactly the recorded size, carries trailing
  bytes, or decodes to anything but that size, and then checks the block's payload digest.

The seal signs the **stored** bytes: the reader checks a block's SHA-512 from the seal before it
decrypts or decompresses anything, so a tampered compressed block is refused as `PayloadCorrupt`
without the decoder ever seeing it.

On the content-streaming suite's test content (baked mesh chunks, texture levels and text),
974 888 bytes are stored as 148 057 (ratio 6.58).

## Pack sets

A game's content need not fit one pack. `Game.kbpackset`, a small text file beside the packs,
lists them in mount order:

```
21kb-pack-set 1
base Game.kbpack
chunk cell_0_0 Game.cell_0_0.kbpack
chunk night Game.night.kbpack
patch 1 patch-0001 Game.patch-0001.kbpack
```

- Exactly one base, first; then the chunks; then the patches in strictly ascending patch level.
  Labels and paths are unique; paths are relative `.kbpack` paths inside the index's directory.
- Every pack states its role, label and patch level in its own sealed header, and the mount
  refuses an index and a pack that disagree (`PackSetInvalid`). A chunk or patch must name the
  mounted base's catalogue identity (`PackSetBaseMismatch`), so packs of different cooks never
  mix.
- **Chunks** carry content split off the base -- one world cell or one data layer, for example.
  They add assets and may not redefine one the base or another chunk already has. Their runtime
  manifests are partial; the default map and every dependency are checked across the whole set.
- **Patches** replace every asset they list (same id or same virtual path) together with its
  artifacts, replace auxiliary files of the same path and supply the project settings. Later
  patches win.
- **Tombstones.** A patch's manifest may also list asset ids and auxiliary file paths to
  remove. They are applied before the patch's own entries, so an asset removed by one patch can
  come back in a later one, and a file can become an asset of the same path. Each tombstone must
  name something the packs before it hold (`PackSetInvalid` otherwise), only a patch may carry
  them, and dependencies are checked after removal, so a removal that strands an asset depending
  on the removed one is refused (`DependencyMissing`). The tombstones live in the patch's
  manifest block, which the seal signs like every other block.
- A packaged player mounts the set when `Game.kbpackset` sits beside `Game.kbpack`, and the
  single pack otherwise. Mounting is all or nothing.

### Chunks of a partitioned world

A chunk rule (`AssetPackChunkRule`) takes assets by virtual path prefix, by world region, or both.
A world region (`AssetPackWorldRegion`) names a partitioned world by its descriptor's virtual path,
an inclusive range of cells and, optionally, data layers. The split reads the world's built cell
index from the cooked pack and moves:

- the cell scene of every unit whose cell lies in the range and whose layer is listed (every layer
  when none is);
- the HLOD proxy mesh of every cell in the range, when the base layer is among the layers;
- the persistent (always-loaded) units of the listed layers, only when the region is the whole
  world (no range given).

Everything those depend on -- meshes, materials, the cell index itself -- stays where its own
rule puts it, by default in the base, so the cell index in the base names cells that live in the
chunk and the set's cross-pack dependency check holds. The text form, shared by `kb_cli pack
split --chunk-cells` and packaging, is

```
<label>=<world virtual path>[@<minX>:<minZ>..<maxX>:<maxZ>][#<layer>[,<layer>...]]
```

with `(base)` naming the base layer, for example
`forest_east=/Game/Worlds/Forest.21kbworld@0:-8..7:7` (a region's cells in every layer) or
`night=/Game/Worlds/Forest.21kbworld#night` (one layer of the whole world). Several rules may use
one label; they make one chunk. A cell or proxy goes to the first rule that takes it, prefix and
region rules alike. `ParseAssetPackWorldRegion` reads the same text in C++.

## Signing, release binding and rollback

Every pack of a set is sealed with the release key, and a packaged player passes its trust
anchor to every pack it mounts: an unsigned or foreign chunk or patch is refused exactly like an
unsigned base.

In a Windows or Linux release the pack set index is a critical file of the signed release
manifest. At startup the player hashes the index against the manifest and binds every pack the
index names to the release through its seal digest, so:

- a pack cannot be swapped, added to or dropped from the set without breaking the manifest;
- the mount order and patch levels are part of what the release key signs;
- an older patch cannot be put back over a newer one: a patch release carries a higher release
  number than the release it patches, and with `--anti-rollback` the player refuses any release
  number below the highest it has run.

`kb_cli pack set-verify --anchor <anchor> Game.kbpackset` mounts a set the way the player does and
reads every block of every pack.

### Encrypted pack sets

Every release's trust anchor carries a fresh content key, and the packs a release seals are
encrypted under it. A patch release ships the packs of the release it patches byte for byte, so
they stay encrypted under the keys they were sealed with. Its pack set index gives each of them a
key line:

```
key <144 hexadecimal digits> Game.kbpack
```

The digits are the pack's own content key encrypted with XChaCha20-Poly1305 under the new
release's anchor key (a random nonce, the key, the tag). The player unwraps it with its anchor
key and mounts the pack with the result; the unwrapped key must have the id the pack's signed
seal records, so a key line cannot be moved to another pack, and one that was altered or made
under another release's key does not unwrap (`ContentKeyMismatch`). A key line before a pack
line, for a pack the index does not list, or a second one for the same pack is refused. The index
itself is signed through the release manifest.

`kb_cli pack set-keys [--content-key <new release key>] --previous-release <dir> Game.kbpackset`
writes exactly the key lines an index needs: it reads each pack's seal, finds the key it was
encrypted under among the keys of the previous release (the content key in its player's anchor,
and every key line of its index, unwrapped with that one) and wraps it under the new key. A pack
sealed under the new key, or not encrypted, gets no line. `kb_cli pack patch --current-release
<dir>` reads the current content with the keys of that release's player. Packaging runs both for
every `--patch-from` release, so patches and `--encrypt-pack` combine, and chains of patch
releases carry every earlier key forward.

## Background loading

Everything the engine loads in the background goes through one service,
`BackgroundLoadService`: one pool of threads in the process (3 by default) serves the content
streamer's pack block reads, the asset manager's asynchronous loads (`RequestLoadAsync`,
`LoadAsync<T>`) and the renderer's texture decodes (`RequestAsyncTextureDecode`). Each user takes
the instance `BackgroundLoadService::Shared()` hands out; the threads start with the first user
and are joined when the last one lets it go. Frame compute stays on the ECS worker pool, which
never waits for a disk.

The service runs two kinds of request.

**Reads** are served highest priority first, ties in submission order. On Windows every file is
opened with `FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING`: a request is widened to the volume's
sector alignment and read into a sector-aligned buffer, bypassing the system cache, and each
worker keeps several reads in flight (4 by default). Elsewhere workers use `pread`. A file that
refuses unbuffered access is read buffered instead. A read carries a transform that runs on the
worker: `ReadPackBlockAsync` verifies the block's seal hash, decrypts and decompresses it there.
The service keeps a file open once read; the streamer lets go of its packs' files when it
releases its resources.

**Jobs** are callables that load or decode something: an asset loader with its dependency
validation, an image decoder. A job may take long or block on its own I/O, so at most two run
at once (`jobWorkers`) and, with more than one worker, never on every worker: one worker always
stays free for reads, so a slow or blocked loader cannot starve streaming. Under that limit a
free worker takes a waiting job before reads, so a busy stream cannot starve the jobs either.
Jobs submitted to a **lane** run one at a time, highest priority first. Each `AssetManager`
opens a lane on its first asynchronous request (its loaders run one at a time, as they always
have), and texture decodes share one. A lane's owner cancels what it queued and waits for its
one running job: the asset manager does so before it changes its loaders or its registry and
when it is destroyed, the texture decoder at process exit. No job outlives what it uses, and no
thread outlives the last user of the service.

Requests of either kind can be re-prioritised or cancelled until a worker takes them. Nothing
waits for a result: the render thread polls read handles, `AssetManager::PumpAsyncLoads` commits
finished asset loads on the owner thread, and a decoded texture lands in the cache that
`TryAcquireDecodedTexture` reads. On the suite's machine a 2 MiB compressed, sealed block streams
in about 1 ms from a file and 0.4 ms from a pack mounted in memory.

Why one service, grown from the streaming I/O pool: the engine used to run three background
loaders, a thread per asset manager, a texture decode thread and the streaming I/O pool. The I/O
pool already had what the other two either lacked or repeated: priorities, cancellation,
re-prioritisation, completion handles and the platform read path. Adding jobs and lanes to it
removed the two single-thread queues and left the read path, which the streaming priority and
budget tests pin down, as it was. Moving the loaders onto the ECS worker pool instead was
rejected: that pool runs each frame's systems, and a loader blocked on a disk or a lock would
stall the frame.

## Residency and budgets

`StreamingResidencyManager` keeps the levels of every streamed resource under one byte budget.
A resource is a list of levels, finest first. Its coarsest levels (the **floor**: a texture's mip
tail, a mesh's coarsest level of detail) are loaded with it and never evicted; finer levels are
always resident as one contiguous run.

- **The budget is never exceeded.** Bytes are committed when a load is issued, so resident bytes
  plus bytes in flight stay within the budget. Only the floors themselves can exceed it, and
  then nothing else streams.
- **Priorities.** Each frame the renderer reports, per resource, the finest level it wants and a
  priority (its screen coverage). Loads go highest priority first, and within a resource
  coarser levels first, so a texture sharpens from its tail upwards.
- **Eviction is LRU by priority.** To make room, the manager first drops levels finer than their
  resource now wants, least recently wanted first; then wanted levels of resources with a lower
  priority than the load, lowest priority first. A load never evicts anything as important as
  itself, so two resources cannot thrash each other. A resource not wanted for 120 frames falls
  back to its floor.
- A failed load is retried after 30 frames.

In the suite's randomised run (2000 frames, 5184 loads, 5161 evictions) the peak committed bytes
equalled the 6600-byte budget and never passed it.

## Texture mip streaming

The texture baker (version 5) keeps every level whose larger edge is at most 128 texels in the
primary block, behind a 20-byte `21KBTXST` header that records the full chain, and writes each
larger mip as its own `Streaming` block (`mip0`, `mip1`, …). A packaged texture loads with its
tail only; the streamer asks for the mip whose size covers the texture's on-screen size (one
level per halving), plus `textureMipBias`. The package validator checks that a streamed texture's
blocks compose into exactly one chain.

## Mesh level-of-detail streaming

A packaged baked mesh with more than one level of detail, whose finer levels hold at least
64 KiB of geometry, loads with its coarsest level only. The streamer asks for the coarsest level
whose simplification error projects to at most `meshMaxScreenErrorPixels` (1 pixel) and loads the
chunks of the missing levels; the mesh keeps its full bounds as levels arrive.

## Renderer settings

`Renderer::ConfigureContentStreaming(RuntimeContentStreamingSettings)`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `budgetBytes` | 512 MiB | GPU memory for streamed textures and meshes, floors included |
| `maxLoadsInFlight` | 32 | Level loads in flight across all resources |
| `maxRebuildsPerFrame`, `maxUploadBytesPerFrame` | 8, 64 MiB | Spreads a burst of arrivals over frames |
| `meshMaxScreenErrorPixels` | 1 | Mesh detail target |
| `textureMipBias` | 0 | Positive keeps textures coarser |

`Renderer::ContentStreamingStats()` reports the residency counters, loads in flight, rebuilds,
uploaded bytes and the last and slowest load latency. In the renderer suite a 512×512 texture
(two streamed mips) and a four-level mesh stream fully in within three frames (about 50 ms in
total; the slowest single level, read, verified and decoded on the background load service, about 20 ms), and a
far camera with a small budget evicts both back to their floors.

## Tools

| Command | Does |
| --- | --- |
| `kb_cli pack info <pack>` | Format, role, label, patch level, identities, compressed blocks, sizes, seal |
| `kb_cli pack compress [--level n] <in> <out>` | Rewrites a pack with another compression level |
| `kb_cli pack split --base <base> --chunk <label>=<prefix>[,…] [--chunk-cells <label>=<region>] [--index <set>] <cooked>` | Splits a cooked pack into a base and chunk packs by virtual path prefix and world region |
| `kb_cli pack patch --current <set or pack> [--current-release <dir>] --patch-level n --output <patch> <new cook>` | Builds a patch pack with what the new cook changed, added and dropped (tombstones) |
| `kb_cli pack set-keys [--content-key <file>] --previous-release <dir> <Game.kbpackset>` | Writes the key lines of packs encrypted under an earlier release's keys |
| `kb_cli pack set-verify [--anchor <file>] <Game.kbpackset>` | Mounts a set as the player does and reads every block |

Split and patch output is unsealed; sign each pack with `kb_cli pack sign` afterwards.

## Packaging

`scripts/package_game.py`:

- `--pack-compression-level <0-19>` (default 9) is passed to the cooker.
- `--pack-chunk LABEL=/Game/PREFIX[,/Game/PREFIX…]`, repeatable, splits the cooked pack into
  `Game.kbpack` and `Game.<label>.kbpack` and writes `Game.kbpackset`.
- `--pack-chunk-cells LABEL=/Game/WORLD.21kbworld[@MINX:MINZ..MAXX:MAXZ][#LAYER,…]`, repeatable,
  puts a world region's or data layer's cells into chunk `LABEL` the same way.
- `--patch-from <previous release directory>` ships the packs of that release byte for byte plus
  one new patch pack `Game.patch-<level>.kbpack`, and an index that adds it. `--patch-level`
  defaults to one above the release's highest patch. The release number must be higher than the
  previous release's.

Every pack the job creates is sealed and verified against the trust anchor, the staged set is
verified with `kb_cli pack set-verify`, and then the release manifest is signed over the finished
stage, index and packs included.

## Limits

- Packaging produces pack sets, patches and encrypted packs for Windows and Linux packages, the
  platforms whose players verify a signed release manifest; Android and web packages ship one
  pack. For a Linux Release package, `--patch-from` names the extracted release archive.
- `--patch-from` cannot be combined with `--pack-chunk` (a patch keeps the chunks of the release
  it patches).
- Only packaged content streams. A loose project in the editor or a development player loads
  full mip chains and every level of detail.
