# AI-upscaled texture pack (disc-extracted) — design

Date: 2026-11-15. Status: draft for approval.

## Intent

Make *Need for Speed: Most Wanted* (Xbox 360, played through SpeedBreaker on Linux) look better at
4K by replacing the game's textures with AI-upscaled versions of themselves. The goal is **the
original art, sharper and cleaner** — not a remaster: no new detail invented, no change of look,
colour or style. The player builds the pack locally from their own disc; the SpeedBreaker
AppImage ships only the code that builds and loads it.

## Goals

1. **Extract** every texture the game can load, straight from the installed disc files
   (`NFS/ZDIR.BIN` + `NFS/ZZDATA*.BIN`), without playing.
2. **Upscale** the colour textures 4x with a conservative AI model on the local GPU (RX 9070 XT,
   Vulkan), cap them at 2048 px on the long side, and compress to BC7 with a full mip chain.
3. **Load** the replacements in SpeedBreaker's Vulkan renderer whenever the game uses the matching
   original, with no visible stutter and no change in behaviour for textures that have no
   replacement.
4. A **Settings > Graphics > HD Textures (Off / Auto / 1K / 2K)** row, default Auto when a pack
   is installed; hidden when there is none (my call; keeps the menu unchanged without a pack).
5. One command builds the whole pack: `AppRun --texpack build` (§6).
6. **Runs well on every machine the AppImage is copied to.** The player said the AppImage will be
   shared across multiple machines and asked that the scripts detect the hardware and run the best
   way for each. So both the pack builder and the in-game loader detect the hardware and tune
   themselves (§7), and one pack works on every machine.

## Non-goals

- Remastering: no AI "detail" models, no PBR, no new normal maps, no relighting.
- Upscaling movies (the existing *Movie Upscaling: Sharper* handles those), render targets,
  resolves, shadow maps, or anything the GPU draws at run time.
- Shipping or sharing the pack. It is derived from EA's art: it stays in the player's data folder.
- Windows, macOS, iOS builds of the loader (the code should not prevent it, but only Linux is built
  and tested here).
- Texture dumping during play (the "dump while playing" approach); see Approaches.

## Anti-goals (a failure even if it "works")

- The game looks *different*: colour shifts, oversharpened halos, painted/plastic surfaces,
  changed alpha edges on foliage, fences, decals or text.
- A replacement lands on the wrong texture (garbage on a car or road).
- Frame pacing gets worse: hitches when the city streams in, or VRAM pressure causing evictions.
- The verified install is modified in any way (SpeedBreaker checks every file's SHA-256).
- The AppImage contains any game-derived data.

## Constraints

- The disc files are read-only inputs; nothing in `game/` changes.
- Xbox 360 texture data is stored **tiled** and **big-endian** (usually 8-in-16 swapped for DXT).
  The renderer already untiles it (`Tiled2D` in `runtime/gpu/renderer.cpp`) and knows the Xenos
  formats it accepts (`GetTextureFormat`: BC1/2/3/5, 8888, 565, 1555, 4444, R8, RG8, 16-bit and
  float formats).
- The renderer's texture cache is keyed by **guest address and fetch-constant fields**, not by
  content (`s_textures`, `GetTexture`-path around renderer.cpp:4960). Textures are (re)loaded when
  their guest pages are written (`writewatch`), i.e. every time the game streams new art into the
  same memory.
- VRAM varies by machine: 16 GB on this build machine's RX 9070 XT, 16 GB *shared* with the CPU
  on a Steam Deck, as little as 2–4 GB on older cards and integrated graphics. 4x linear = 16x
  texels; BC7 is 1 byte/texel. The loader's budget must adapt (§7).
- Tools that run during the pack build must be built from source or come from the distro's repos
  (no prebuilt binaries downloaded from the web in this environment). Model **weights** are data
  files and may be downloaded.
- The AppImage is shared across machines, so the bundled tools must run on any x86-64-v3 Linux
  with Vulkan 1.2, like the game itself (same glibc/libstdc++ bundling as `make_bundle.sh`).
  Model weights are downloaded on first use rather than bundled, and only models whose licence
  allows redistribution are offered (my call: it keeps sharing the AppImage, or a pack-free copy of
  the tools, unproblematic).
- Build machine: Ryzen 9 7900X3D (24 threads), RX 9070 XT (RADV).

## Approaches considered

| | How | Trade-off |
|---|---|---|
| A. Dump while playing | Renderer saves each texture as it loads, keyed by content hash; pack built from the dumps. | Format-agnostic and certain to match, but covers only what the player has seen. |
| **B. Extract from disc (chosen)** | A tool reads ZDIR/ZZDATA, finds every TPK, decodes its textures; renderer matches replacements by content hash. | Full coverage without playing; costs reverse-engineering of the 360 archive and TPK layouts, and the hash must be computed exactly as the renderer sees the data. |
| C. Repack bigger textures into the game files | Write larger TPKs back. | Rejected: breaks the 512 MB guest memory budget and the install's file verification. |

The player chose **B**. A's runtime dump is kept as a *debug aid only* (see Verification), because
it is the cheapest way to prove B's hashes match.

## Design

### 1. Matching key: content hash of the guest texture data

A replacement is identified by **XXH3-64 of the texture's base-level bytes exactly as they sit in
guest memory** (tiled, endian-swapped as on disc), plus its Xenos format, width and height:

```
key = XXH3_64(base_level_bytes) ^ mix(format, width, height)
```

Why content, not name: TPK textures carry EA name hashes, but the renderer only sees fetch
constants (an address). Mapping address→name would need a mid-asm hook in the game's TPK loader
(the recomp supports `[[midasm_hook]]`, see `config/nfsmw.toml`), which is more reverse
engineering and breaks whenever memory is reused. Content hashing needs no game hooks, and TPK
data in the archives is stored uncompressed and is loaded into guest memory verbatim (to be
confirmed by the spike, step 0).

The extractor and the renderer must agree on "base-level bytes": both compute the byte range from
(format, width, height, tiled, pitch) with **one shared header** (`runtime/gpu/texpack_layout.h`)
used by the renderer and by the tool, so the size/alignment rules cannot drift apart.

Mip levels are not hashed (the base level identifies the texture); the replacement brings its own
mip chain.

### 2. Disc extraction tool (`tools/texpack/extract`)

C++ (shares headers with the runtime), built by CMake as a separate target, not linked into the
game.

1. Read `NFS/ZDIR.BIN`: 24-byte little-endian entries `{name_hash, archive, sector, size, ?, ?}`,
   sorted by hash; data at `ZZDATA<archive>.BIN` offset `sector * 0x800`.
   (`runtime/game/language.h` documents the hash: EA's `h = h*33 + c` from 0xFFFFFFFF, upper case.)
   Names are optional — output is keyed by content hash; a name list, where known, is only for
   readable logs.
2. Walk each file's chunk tree (8-byte headers `{id, size}`, little-endian; ids with bit 31 set
   are containers) and collect every TPK (`0xB3300000`), including TPKs nested inside other
   files (car `.BIN`s, `.BUN` bundles, the city stream files). Recon so far: 247 files start with a
   TPK (≈150 MB); others embed them.
3. For each TPK: `0x33310001` header (TPK name), `0x33310002` name-hash table, `0x33310004`
   texture info records, `0x33310005` compression/format info; pixel data in
   `0xB3320000 > 0x33320003`. Record layout for the 360 build is to be confirmed (step 0); fields
   needed: name hash, data offset, data size, width, height, format, mip count, tiling.
4. Per texture: compute the content key (§1), skip duplicates (the same texture appears in many
   TPKs), and write:
   - `raw/<key>.bin` — nothing; *not* kept (only the decode is needed),
   - `src/<key>.png` — the decoded base level (untiled, endian-fixed, BC-decoded, RGBA8),
   - an entry in `manifest.json`: key, format, size, mip count, TPK name, texture name hash,
     classification (below).
5. **Classification** — decides what gets upscaled:
   - `color` → upscale (BC1/BC2/BC3/8888/565/1555/4444 textures that are not below).
   - `normal` → **skip**: BC5 (DXN), and textures named/flagged as normal or bump maps.
   - `data` → **skip**: R8/RG8/16-bit/float formats, lookup/ramp textures, anything ≤ 16 px on a
     side, and textures whose width or height is 1.
   - `ui` → upscale with the same model (fonts and HUD art benefit), listed separately so they can
     be excluded in one switch if they look wrong.

### 3. Upscaling (`tools/texpack/upscale`)

- Engine: **Real-ESRGAN's ncnn-Vulkan implementation, built from source** against the distro's
  `ncnn` package. Fallback if it cannot be built: `waifu2x-ncnn-vulkan` from the distro repos.
- Model: chosen by the player from a **side-by-side comparison** of 2–3 conservative 4x models on
  ~12 representative textures (car body, road, building facade, sign text, tree/fence with alpha,
  HUD). The pick is a **one-time decision by this player, made during the build** (my call): the
  comparison page is throwaway, and the tool then hard-codes that one model (file names and
  SHA-256 of its weights). Candidates must already exist as **ncnn weights** (`.param`/`.bin`):
  converting PyTorch `.pth` weights needs PyTorch/pnnx, which isn't available here. Candidates:
  `realesrgan-x4plus` from the official ncnn release (the least faithful, kept as a reference),
  ncnn-format community "sharp but faithful" 4x models, and waifu2x (`models-cunet`/`upconv_7`) as
  the most conservative. Shown in the browser pane as a slider comparison; nothing proceeds to the
  full run until one is picked.
- Alpha: the RGB is upscaled by the model; alpha is upscaled separately (the model on alpha as
  grey, or Lanczos for 1-bit alpha), then thresholded back to 1-bit for BC1-with-alpha sources so
  cut-out edges stay crisp.
- Tiling seams: textures that wrap (roads, walls) are padded with wrapped borders before the model
  and cropped after, so the upscaled texture still tiles.
- Size: output = 4x, then downscaled (Lanczos) so the long side ≤ 2048. A texture already ≥ 1024
  on its long side is upscaled to 2048 at most (i.e. 2x effective). The pack is always built at
  this full size, whatever machine builds it; smaller machines drop the top mip levels at load
  time (§7), so one pack serves them all.
- Colour: output is checked against the source by mean colour per channel; a texture that drifts
  more than a small threshold is flagged in the report (and falls back to the original).

### 4. Compression and pack format

- Encoder: **bc7enc_rdo built from source** (BC7 for colour, keeps alpha). Full mip chain
  generated from the upscaled image (box/Kaiser filter, alpha-coverage-preserving for cut-outs).
- Files: `~/.local/share/speedbreaker/texture-pack/<key>.dds` (DX10 header, BC7_UNORM), plus
  `pack.json` (version, model name, creation date, count, the game region it was built from).
- Region: the pack is built for one disc (USA here); `pack.json` records which, and the loader
  ignores a pack built for another region.

### 5. Runtime loader (renderer)

- At startup, if HD Textures is on and `texture-pack/pack.json` exists: index the pack (keys from
  file names; no files opened yet).
- In the texture load path (where a `Texture` is created or reloaded after its pages were
  written), for **2D textures with a format the pack can contain**: hash the base level on the CPU
  (XXH3 over the guest bytes in host-mapped guest memory; the untile itself runs on the GPU and
  never reads them on the CPU, so this is a new, cheap read), look the key up.
  - Hit: create the texture's image from the replacement (BC7, its own size and mips) instead of
    untiling the guest data; keep `guestStart/guestSize` and the write-watch exactly as now, so a
    later write to those pages reloads and re-hashes as usual. Swizzle views still come from the
    fetch constant.
  - Miss: unchanged path.
- Never replaced: scaled textures, resolve targets (`s_directByBase` / resolve-to-texture),
  shadow maps, cubes, volumes, stacked textures, textures with offsets (`offX/offY`).
- Loading: DDS files are read and uploaded on a **background thread** with a small LRU of decoded
  files; the first frame a replacement is missing it uses the original (no stall), and switches
  when ready. (The renderer already has a memory thread and staged uploads to reuse.)
- Memory: replacement images are counted in the existing `[perf] memory:` line and freed with
  their `Texture`.
- Sampler: anisotropy and LOD bias unchanged; mip count comes from the replacement.
- Settings: `HD Textures` row on the Graphics page (Off / Auto / 1K / 2K, default Auto; hidden
  when no pack is installed; Auto shows what it picked, e.g. "Auto (2K)", as Internal Resolution
  does). Changing it drops and reloads textures. `scripts/verify_settings.py` updated for the row.
- Resolution cap: a replacement whose base level exceeds the cap is loaded from the first mip
  level that fits (DDS levels are contiguous, so this is an offset into the file, not a resample).
- Env switches for testing: `NFSMW_TEXPACK=0` (off), `NFSMW_TEXPACK_LOG=1` (per-texture
  hit/miss lines with keys), `NFSMW_TEXPACK_DUMP=<dir>` (debug: dump the key and decoded base
  level of every 2D texture the renderer loads — the approach-A aid for verifying matches).

### 6. Packaging

- The AppImage gains the extractor, the upscaler and bc7enc binaries plus a
  `speedbreaker-texpack` script, run as `AppRun --texpack build|status|remove` (AppRun handles
  `--texpack` itself before handing anything else to the game's launcher), so building the pack
  needs nothing installed. The binaries use the bundle's own `lib/` (ncnn linked statically)
  through the same loader logic as the game. Model weights are fetched on first use, with their
  SHA-256 checked, into `~/.cache/speedbreaker/models/`.
- `scripts/linux/make_appimage.sh` adds them; licences (Real-ESRGAN BSD-3, ncnn BSD-3, bc7enc
  MIT/public domain, the model's licence) go in `licenses/`.
- **Copying a pack between machines:** `--texpack build` can run on the fastest machine, and the
  `texture-pack/` folder copied to the others (`--texpack status` prints its location, size and
  region). Packs are hardware-independent (§7).

### 7. Hardware detection and tuning

Both halves detect the machine at start and log what they chose and why (a `[texpack]` line), and
every choice can be overridden by an environment variable.

**In the game (loader).** Detected from Vulkan: device type (discrete / integrated), device-local
heap size, BC7 support, the Steam Deck/Steam Frame (the renderer already detects these for other
settings), and the existing Internal Resolution choice.

| Machine | Auto picks | Why |
|---|---|---|
| Discrete GPU, ≥ 8 GB VRAM | 2K, all replacements | The full pack fits with room to spare. |
| Discrete GPU, 4–8 GB | 1K | Halves each side (¼ the memory); still 2–4x the original. |
| Discrete GPU, < 4 GB | 1K, and only textures ≥ 128 px originally | Spend memory where it shows most. |
| Integrated GPU / APU, incl. Steam Deck | 1K, and only while Internal Resolution is ≤ 1.5x | Shared memory and bandwidth; 2K detail is invisible on an 800p screen. |
| No BC7 support | Off | Never seen on Vulkan 1.2 desktop GPUs; logged if it happens. |

On top of the table, a **VRAM budget**: replacement images may use at most 50% of the
device-local heap left after the game's own allocations (as reported in the `[perf] memory:`
line). Beyond it, the least-recently-used replacements fall back to their original texture until
memory frees up, so the game never fails to allocate because of the pack. 1K/2K/Off in the
settings override Auto; the budget always applies. `NFSMW_TEXPACK_CAP=<px>` and
`NFSMW_TEXPACK_BUDGET_MB=<n>` override for testing.

**In the pack builder.** Detected before the run: the Vulkan devices (the upscaler picks the
discrete GPU, as the game does, and honours `NFSMW_GPU`), their memory, the CPU thread count, free
RAM and free disk space.

| Detected | Builder does |
|---|---|
| GPU memory | Picks the upscaler tile size (largest that fits: e.g. 400 px tiles at ≥ 12 GB, 200 at 4–8 GB, 100 below) and how many images it upscales in parallel. |
| Integrated GPU only (APU, Steam Deck) | Warns that the build will take hours and suggests building on another machine and copying the folder; continues if the player confirms (`--yes` skips the prompt). |
| No usable Vulkan GPU | Refuses to upscale (CPU upscaling would take days); extraction still works. |
| CPU threads | Runs extraction/decoding and BC7 compression with one worker per thread (BC7 is the CPU-heavy step). |
| Free disk | Checks the estimated pack size (from the extracted count) plus working space before starting; refuses if short. |
| Free RAM | Limits parallel BC7 jobs so the run never swaps. |

The build can be **interrupted and resumed**: each finished texture is written atomically, and a
re-run skips keys already in the pack (`--force` redoes them).

## Verification

0. **Spike first (throwaway, before any of the above is built for real):** extract one TPK used in
   the first race, hash its textures with the shared layout code, run the game with
   `NFSMW_TEXPACK_DUMP`, and confirm the keys match. Also confirms: the 360 texture-info record
   layout, that archive TPK data is uncompressed and loaded verbatim, and the tiling/endian of DXT
   data. If keys don't match, the fallback is a mid-asm hook on the TPK loader (address→name) — a
   design change brought back to the player.
1. Extractor unit tests on synthetic TPKs (chunk walk, record parse, untile/endian round-trip
   against the renderer's own untile).
2. Coverage report after a full extract: textures found per TPK, duplicates, per-class counts; and
   after a 15-minute drive with `NFSMW_TEXPACK_LOG=1`: hit rate for colour textures (target ≥ 95%,
   misses listed by key for follow-up).
3. Visual check: before/after screenshots at 4K of the same spots (garage, a race start, the city
   at noon), plus the model comparison page.
4. Performance: `[perf]` worst-frame and memory lines with the pack on vs off over the same drive;
   no new frames over 33 ms attributable to texture loads.
5. Hardware tuning: the detection logic is pure functions of the detected values (as
   `runtime/gpu/shadow_auto.h` and `anisotropy_auto.h` do today) with unit tests for each table
   row. On this machine, also run forced low-end profiles (`NFSMW_GPU=` the integrated Radeon 610M,
   and `NFSMW_TEXPACK_BUDGET_MB=1024`) to see the budget and fallback work for real.

Runs 0, 2 and 4 can be scripted with `NFSMW_INPUT_SCRIPT` (as `scripts/bench.sh` does), so they
don't need the player at the controls.

## Left to the builder

- Exact 360 TPK record layout and any per-section compression in the city stream files.
- Thread/queue details of async replacement uploads within the renderer's existing structure.
- The colour-drift threshold and alpha thresholding constants.
- Where in the renderer the hash is computed so it is done once per (re)load, not per draw.

## Calls made without asking (check these)

- Content-hash matching instead of a game hook (§1).
- 2048 px cap, BC7, full mips (§3–4).
- UI/HUD textures are upscaled too, but switchable as a group (§2.5).
- Normal maps and data textures are never AI-processed (§2.5).
- The upscaler and encoder are bundled in the AppImage; weights downloaded on first use, and only
  models with redistributable licences (§6, Constraints).
- One pack for all machines, always built at 2K; smaller machines load lower mips (§3, §5, §7).
- The hardware tiers and thresholds in §7 (8 GB / 4 GB, 50% budget, tile sizes).
- The integrated Radeon 610M on this PC stands in for a low-end machine in testing (§Verification 5).
- HD Textures defaults to Auto when a pack exists (§5).
