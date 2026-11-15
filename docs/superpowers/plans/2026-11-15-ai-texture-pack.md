# AI Texture Pack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract every texture from the player's installed Xbox 360 disc files, upscale the colour
textures 4x with a faithful AI model into a BC7 pack, and have SpeedBreaker's Vulkan renderer load
them by content hash, tuned automatically for each machine the AppImage runs on.

**Architecture:** A shared header gives the renderer and the offline extractor one definition of a
texture's guest bytes and its content key (XXH3-64). The extractor (`texpack/`, C++) reads
`ZDIR.BIN`/`ZZDATA*.BIN`, walks TPK chunks, decodes textures to PNG and writes a manifest; a Python
driver runs Real-ESRGAN-ncnn-Vulkan and a C++ encoder (bc7enc_rdo) with hardware-tuned
parallelism; the renderer indexes the pack, hashes each eligible texture on (re)load and swaps in
the BC7 replacement asynchronously under an auto-chosen resolution cap and VRAM budget.

**Tech Stack:** C++20 (clang), CMake/Ninja, Vulkan, xxHash 0.8 (vendored header), stb_image /
stb_image_write (vendored), bc7enc_rdo, Real-ESRGAN-ncnn-Vulkan + ncnn (built from source),
Python 3 stdlib (driver), existing `sb_test` CTest harness.

**Spec:** `docs/superpowers/specs/2026-11-15-ai-texture-pack-design.md` (one deviation: the tool sources live in a top-level `texpack/`, not `tools/texpack/`, because `tools/` is git-ignored as the toolchain folder; built binaries of third-party tools do go in `tools/texpack/`).

## Global Constraints

- Never modify anything under the install's `game/` folder; disc files are read-only inputs.
- No game-derived data in the repository, the AppImage, test fixtures or logs (fixtures are synthetic).
- Pack location: `~/.local/share/speedbreaker/texture-pack/` (`<key>.dds` + `pack.json`); model weights in `~/.cache/speedbreaker/models/`.
- Content key: `XXH3_64(base_level_bytes) ^ mix(format, width, height)`, base level as in guest memory (tiled, endian-swapped as on disc); mips are not hashed.
- Pack is always built at full size: 4x, long side ≤ 2048; BC7_UNORM, DX10 DDS header, full mip chain.
- Never AI-processed: BC5/DXN, normal/bump maps, R8/RG8/16-bit/float formats, anything ≤ 16 px on a side, width or height 1.
- Never replaced at run time: scaled textures, resolve targets, shadow maps, cubes, volumes, stacked textures, textures with offX/offY.
- Settings row: Graphics > `HD Textures` = Off / Auto / 1K / 2K, default Auto, hidden with no pack.
- Env switches: `NFSMW_TEXPACK=0`, `NFSMW_TEXPACK_LOG=1`, `NFSMW_TEXPACK_DUMP=<dir>`, `NFSMW_TEXPACK_CAP=<px>`, `NFSMW_TEXPACK_BUDGET_MB=<n>`.
- Loader Auto tiers: discrete ≥ 8 GB → 2K; discrete 4–8 GB → 1K; discrete < 4 GB → 1K and only originals ≥ 128 px; integrated/APU/Steam Deck → 1K only while Internal Resolution ≤ 1.5x; no BC7 → Off. Budget: ≤ 50% of device-local heap left after the game's own allocations.
- Builder tiers: tile 400 px at ≥ 12 GB GPU memory, 200 at 4–8 GB (and 8–12 GB), 100 below 4 GB; integrated-only → warn + confirm (`--yes`); no Vulkan GPU → extract only; BC7 workers = CPU threads, limited by free RAM; refuse when disk is short.
- Only models whose licence allows redistribution; weights SHA-256-checked.
- Bundled binaries must run on any x86-64-v3 Linux with Vulkan 1.2 (same glibc/libstdc++ bundling as `make_bundle.sh`).
- Work on a local branch `texpack`; commit per task; never push.

## Review Focus

1. **Same address, new texture** (the city streams new art into memory a replacement was hashed from) → the reload re-hashes and drops or swaps the replacement; never shows the old HD texture on new data. Test: Task 12 step "rehash on rewrite".
2. **Damaged pack file** (a `.dds` truncated by an interrupted copy between machines) → that texture falls back to the original, logged once, game keeps running. Test: Task 10 `RejectsTruncatedDds`.
3. **Pack from another disc region** → ignored with one log line. Test: Task 10 `IgnoresOtherRegion`.
4. **Build interrupted or disk full mid-run** → re-run resumes, no half-written `.dds` ever counted as done. Test: Task 9 `test_atomic_resume`.
5. **Low-memory machine** (2 GB card, Steam Deck) → Auto picks the tier and the budget evicts instead of failing allocations. Test: Task 11 tier table + Task 12 forced-budget run.

---

### Task 1: Shared texture layout header (no behaviour change)

**Files:**
- Create: `runtime/gpu/texture_layout.h`
- Modify: `runtime/gpu/renderer.cpp` (the size/pitch computation in the texture fetch path, ~lines 4878–4905; `Tiled2D` ~line 3953; `GetTextureFormat` ~3918)
- Test: `tests/texture_layout_test.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces (namespace `gpu::texlayout`, header-only, Vulkan-free):
  - `struct FormatInfo { uint32_t bpbLog2, block, convert; int vkFormat; };`
  - `bool GetFormat(uint32_t xenosFormat, FormatInfo& out);` (moved from `GetTextureFormat`; `vkFormat` holds the `VkFormat` value as int)
  - `uint32_t Tiled2D(uint32_t x, uint32_t y, uint32_t pitchAligned, uint32_t bppLog2);` (moved)
  - `struct Base { uint32_t pitchAligned, rowBytes, heightAligned, size; };`
  - `Base BaseLevel(const FormatInfo&, uint32_t width, uint32_t height, uint32_t pitchBlocks, bool tiled, uint32_t offX, uint32_t offY);` — the 2D, single-layer base-level byte range exactly as the renderer computes it today.

- [ ] **Step 1:** `git switch -c texpack`. Write `tests/texture_layout_test.cpp` (CHECK macro style of `anisotropy_auto_test.cpp`): `BaseLevel` for DXT1 (format 18) 256x256 tiled pitch 64 blocks → `size == 32768`; DXT5 (20) 512x256 tiled → `size == 131072`; 8888 (6) 100x50 linear pitch 128 → `rowBytes == 512`, `size == 512*50`; and a check that reads `renderer.cpp` (argument) and asserts it calls `texlayout::BaseLevel(` and no longer defines `uint32_t Tiled2D(`. Register `sb_test(texture_layout_test ARGS ${RT}/gpu/renderer.cpp)`.
- [ ] **Step 2:** Build with `-DSB_TESTS=ON`; `ctest --test-dir build/main -R texture_layout` → FAIL (header missing).
- [ ] **Step 3:** Create the header by moving the existing code verbatim (same arithmetic, including the 32-alignment and `<< bpbLog2`); renderer calls it.
- [ ] **Step 4:** `ctest -R texture_layout` → PASS; `ninja -C build/main SpeedBreaker` builds; run the game 60 s with `NFSMW_TEXTURE_VERIFY=1` and confirm no new `[renderer]` errors in `~/.local/share/speedbreaker/last-run.log`.
- [ ] **Step 5:** Commit `gpu: texture_layout.h, the base level's bytes for the renderer and texpack`.

### Task 2: Content key

**Files:**
- Create: `runtime/thirdparty/xxhash/xxhash.h` (copy of xxHash 0.8.x, BSD-2, with its LICENSE), `runtime/gpu/texpack_key.h`
- Test: `tests/texpack_key_test.cpp`; `THIRD_PARTY_NOTICES.md` (add xxHash)

**Interfaces:**
- Produces: `uint64_t gpu::texpack::Key(const uint8_t* base, size_t size, uint32_t format, uint32_t width, uint32_t height);` and `std::string gpu::texpack::KeyName(uint64_t key);` (16 lowercase hex digits).

- [ ] **Step 1:** Test: same bytes + same (format,w,h) → equal keys; one flipped byte → different; same bytes, different width → different; `KeyName(0x1234)` → `"0000000000001234"`; `Key` of 4 MB of zeros completes and equals itself across calls.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement with `XXH_INLINE_ALL` + `XXH3_64bits`; `mix` = FNV-1a over the three values (as `ScaledSignature` does).
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** Commit.

### Task 3: Renderer dump/log of keys (debug aid, spike half 1)

**Files:**
- Modify: `runtime/gpu/renderer.cpp` (where a `Texture` is created or reloaded after its pages were written)
- Create: `runtime/gpu/texpack_eligible.h`; Test: `tests/texpack_eligible_test.cpp`

**Interfaces:**
- Produces: `bool gpu::texpack::Eligible(const EligibleIn&)` with `struct EligibleIn { bool cube, volume, stacked, scaled, shadowMap, resolveTarget; uint32_t layers, offX, offY, format, width, height; };` — true only for the replaceable cases in Global Constraints (2D, 1 layer, no offsets, not scaled/resolve/shadow) and the formats the pack can hold (18, 19, 20, 6, 14, 4, 3, 15).
- Produces (env): `NFSMW_TEXPACK_LOG=1` prints `[texpack] key <name> fmt <f> <w>x<h> @<addr> <hit|miss>`; `NFSMW_TEXPACK_DUMP=<dir>` writes `<dir>/<name>.txt` (format, size, address) once per key.

- [ ] **Step 1:** Test `Eligible` for each exclusion (cube, volume, stacked, scaled, shadow map, resolve target, layers 2, offX 4, BC5 format 49, 1-px width) → false; DXT1 256x256 plain → true.
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement; in the renderer compute `Key` on the CPU from `g_memory.base + base` with `BaseLevel(...).size` only when either env switch is set (zero cost otherwise). **Step 4:** Test PASS; run the game to the first race with `NFSMW_INPUT_SCRIPT` (see `scripts/bench.sh`) and `NFSMW_TEXPACK_DUMP=$SCRATCH/dump`; expect hundreds of `.txt` files.
- [ ] **Step 5:** Commit.

### Task 4: Archive reader

**Files:**
- Create: `texpack/CMakeLists.txt`, `texpack/archive.h`, `texpack/archive.cpp`; top-level `CMakeLists.txt`: `option(SB_TEXPACK "Build the texture pack tools" OFF)` → `add_subdirectory(texpack)`
- Test: `tests/texpack_archive_test.cpp` (writes a synthetic ZDIR + ZZDATA0 into a temp dir)

**Interfaces:**
- Produces: `struct ArchiveEntry { uint32_t nameHash, archive, sector, size; };` `class Archive { public: bool Open(const std::filesystem::path& nfsDir, std::string& error); const std::vector<ArchiveEntry>& Entries() const; bool Read(const ArchiveEntry&, std::vector<uint8_t>& out) const; };` Reuse `ArchiveHash` from `runtime/game/language.h` for names in logs.

- [ ] **Step 1:** Test: 3 entries (24-byte LE records), data at `sector*0x800` → `Read` returns exact bytes; an entry past the end of its ZZDATA file → `Read` false (the real index has 13 such entries; they're skipped, not fatal); missing ZDIR → `Open` false with message.
- [ ] **Steps 2–4:** FAIL → implement → PASS. **Step 5:** Commit.

### Task 5: TPK parser — layout discovery and the spike gate

**Files:**
- Create: `texpack/tpk.h`, `texpack/tpk.cpp`, `texpack/extract_main.cpp` (CLI `texpack-extract`), `docs/TEXPACK_FORMAT.md` (the 360 layout as found)
- Test: `tests/texpack_tpk_test.cpp` (synthetic TPK built to the documented layout)

**Interfaces:**
- Produces: `struct TexRecord { uint32_t nameHash, format, width, height, mipCount, pitchBlocks, endian; bool tiled; size_t dataOffset, dataSize; std::string tpkName; };` `void FindTpks(std::span<const uint8_t> file, std::vector<std::span<const uint8_t>>& out);` (recursive chunk walk: 8-byte LE `{id,size}`, bit 31 = container, collects `0xB3300000`); `bool ParseTpk(std::span<const uint8_t> tpk, std::vector<TexRecord>& out, std::string& error);`
- Produces (CLI): `texpack-extract --keys-only <nfsDir>` prints one `<keyname> <tpkName> <fmt> <w>x<h>` line per unique texture.

- [ ] **Step 1 (discovery, no code committed):** Write a throwaway script in the scratchpad that walks the TPK used by the first race, dumps `0x33310004`/`0x33310005` records, and correlate fields with Task 3's dump (`fmt`, size, data size). Write the confirmed record layout, endianness and any compressed sections (city stream files, `PADB*`) into `docs/TEXPACK_FORMAT.md` (layout only, no game data).
- [ ] **Step 2:** Test: synthetic TPK (2 textures: DXT1 64x64, 8888 32x32) nested inside a container chunk → `FindTpks` finds 1, `ParseTpk` yields 2 records with the right fields; truncated chunk → error, no crash.
- [ ] **Steps 3–5:** FAIL → implement → PASS.
- [ ] **Step 6 (spike gate):** `texpack-extract --keys-only ~/.local/share/speedbreaker/game/NFS > $SCRATCH/keys.txt`; compare with the Task 3 dump: **≥ 95% of eligible dumped keys must be in `keys.txt`.** If not, STOP and bring the mismatch to the user: the spec's fallback is a mid-asm hook on the TPK loader, a design change.
- [ ] **Step 7:** Commit.

### Task 6: Decode and classify

**Files:**
- Create: `texpack/decode.h`, `texpack/decode.cpp`, `texpack/classify.h`, `texpack/thirdparty/stb/stb_image.h`, `stb_image_write.h` (public domain)
- Test: `tests/texpack_decode_test.cpp`

**Interfaces:**
- Produces: `bool DecodeBase(const TexRecord&, std::span<const uint8_t> data, std::vector<uint8_t>& rgba);` (untile with `texlayout::Tiled2D`, endian swap 8-in-16 / 8-in-32, BC1/2/3 decode, 565/1555/4444/8888 → RGBA8); `enum class TexClass { Color, Normal, Data, Ui };` `TexClass Classify(const TexRecord&, std::string_view tpkName);` (rules from Global Constraints; UI = TPK names under `FrontEnd`/`HUD`/font, as found in Task 5).

- [ ] **Step 1:** Tests: a hand-built BC1 block (c0=0xF800 red, c1=0x001F blue, indices 0) decodes to opaque red; BC1 with c0≤c1 and index 3 → alpha 0; 2x2 tiled 8888 image round-trips through tile→untile; 8-in-16 swap of `01 02 03 04` → `02 01 04 03`; Classify: format 49 → Normal, 16x16 → Data, width 1 → Data, DXT5 512x512 in "CARS" → Color.
- [ ] **Steps 2–4:** FAIL → implement → PASS. **Step 5:** Commit.

### Task 7: Full extraction

**Files:**
- Modify: `texpack/extract_main.cpp`
- Test: `tests/texpack_extract_test.cpp` (synthetic archive → temp out dir)

**Interfaces:**
- Produces (CLI): `texpack-extract <nfsDir> <workDir> [--jobs N]` → `<workDir>/src/<key>.png` for every Color/Ui texture, `<workDir>/manifest.json` = `{"region":"usa","textures":[{"key","class","format","width","height","mips","tpk","nameHash","alpha":"none|1bit|full"}...]}`, plus a summary line `found N, unique U, color C, ui I, skipped S`.

- [ ] **Step 1:** Test: archive with the same texture in two TPKs + one BC5 → one PNG, manifest has 2 entries (BC5 classed Normal, no PNG), alpha detection `1bit` for a BC1-with-alpha source.
- [ ] **Steps 2–4:** FAIL → implement (parallel decode, one worker per thread) → PASS; real run prints the summary.
- [ ] **Step 5:** Commit.

### Task 8: Third-party tools and model comparison (user checkpoint)

**Files:**
- Create: `scripts/texpack/setup_tools.sh` (pinned commits: Real-ESRGAN-ncnn-vulkan, ncnn static, bc7enc_rdo; into `tools/texpack/`), `texpack/models.json`

**Interfaces:**
- Produces: `tools/texpack/bin/realesrgan-ncnn-vulkan`, `tools/texpack/lib/libbc7enc.a` + headers; `models.json` = `[{"id","files":[{"name","url","sha256"}],"scale":4,"license","engine":"realesrgan|waifu2x"}]`.

- [ ] **Step 1:** Write and run `setup_tools.sh`; expect `realesrgan-ncnn-vulkan -h` to print usage and list the RX 9070 XT with `-g`.
- [ ] **Step 2:** List 2–3 candidate models available as ncnn weights with redistributable licences; record them in `models.json` with SHA-256.
- [ ] **Step 3:** Upscale ~12 representative extracted textures (car body, road, facade, sign text, foliage with alpha, HUD) with each; build an HTML before/after slider page in the scratchpad (never published: game-derived) and open it in the browser pane.
- [ ] **Step 4: Ask the user to pick.** Record the chosen `id` as `"default": true` in `models.json`.
- [ ] **Step 5:** Commit (script + `models.json` only).

### Task 9: Encoder and build driver with hardware detection

**Files:**
- Create: `texpack/encode_main.cpp` (CLI `texpack-encode`), `texpack/speedbreaker-texpack` (Python 3, stdlib only), `texpack/hwdetect.py`
- Test: `tests/texpack_encode_test.cpp`, `tests/texpack_driver_test.py` (unittest; registered with `add_test(... python3 -I ...)`)

**Interfaces:**
- Produces (CLI): `texpack-encode --src <orig.png> --up <upscaled.png> --alpha none|1bit|full --wrap --out <key>.dds` → BC7 DX10 DDS, long side ≤ 2048 (Lanczos), full mips (alpha-coverage preserving for 1bit), 1-bit alpha re-thresholded; exit 3 and no output if mean colour per channel drifts > 4/255 from the source (reported as `drift`).
- Produces (Python): `hwdetect.Machine(gpus=[{name, discrete, vram_mb}], threads, free_ram_mb, free_disk_mb)`; `plan_build(m: Machine, est_pack_mb: int) -> BuildPlan(gpu_index, tile, jobs_upscale, jobs_encode, warn_slow, refuse_reason)` per the builder tiers.
- Produces (CLI): `texpack-gpuinfo` (C++, Vulkan) prints JSON `[{"index","name","discrete","vram_mb","bc7"}]` in the same device order the upscaler's `-g` uses; `hwdetect.py` reads it plus `os.cpu_count()`, `/proc/meminfo` MemAvailable and `shutil.disk_usage`.
- Produces (CLI): `speedbreaker-texpack build [--yes] [--force] [--no-ui] [--model ID] [--game DIR]` (`--no-ui` leaves the UI class out, the spec's group switch), `status`, `remove`. Writes `pack.json` = `{"version":1,"region","model","created","count","ui":true|false}`.

- [ ] **Step 1:** Tests (Python): `plan_build` tiers — 16 GB discrete → tile 400; 6 GB → 200; 2 GB → 100; only integrated → `warn_slow`; no GPU → `refuse_reason` mentions extract-only; free disk below estimate → refuse; 4 GB free RAM, 24 threads → `jobs_encode` capped by RAM (≤ free_ram_mb // 512). `test_atomic_resume`: a stray `<key>.dds.partial` is ignored and redone; an existing complete `<key>.dds` is skipped unless `--force`.
- [ ] **Step 2:** Tests (C++): 64x64 RGBA → DDS with 7 mips, DX10 BC7 header, dims 256x256 from a 4x input; 1024x512 source with 4096x2048 upscaled → 2048x1024; drift case returns exit 3.
- [ ] **Steps 3–4:** FAIL → implement (wrap padding = 32 px wrapped border, cropped ×4 after; upscale runs on `BuildPlan.gpu_index`, honouring `NFSMW_GPU` as the game does) → PASS.
- [ ] **Step 5:** Real run on this machine: `speedbreaker-texpack build --yes`; record time, pack size and the summary in the commit message. Interrupt once mid-run and re-run to see it resume.
- [ ] **Step 6:** Commit.

### Task 10: Runtime pack index and DDS reader

**Files:**
- Create: `runtime/gpu/texpack.h`, `runtime/gpu/texpack.cpp`
- Test: `tests/texpack_pack_test.cpp`

**Interfaces:**
- Produces: `class gpu::texpack::Pack { public: bool Open(const std::filesystem::path& dir, std::string_view region, std::string& why); bool Has(uint64_t key) const; struct Image { uint32_t width, height, levels; std::vector<uint8_t> data; std::vector<size_t> levelOffsets; }; bool Load(uint64_t key, uint32_t capPx, Image& out, std::string& why) const; size_t Count() const; };` — `Load` starts at the first mip whose long side ≤ `capPx`.

- [ ] **Step 1:** Tests: synthetic pack of 2 DDS → `Count()==2`, `Has` true/false; 2048x1024 with cap 1024 → 1024x512, levels = original − 1; `RejectsTruncatedDds` → `Load` false with `why`; `IgnoresOtherRegion` → `Open` false when `pack.json` says `pal_es` and region is `usa`; missing `pack.json` → false.
- [ ] **Steps 2–4:** FAIL → implement (index by file name only, files opened on `Load`) → PASS. **Step 5:** Commit.

### Task 11: Auto policy

**Files:**
- Create: `runtime/gpu/texpack_auto.h` (pure, like `shadow_auto.h`)
- Test: `tests/texpack_auto_test.cpp`

**Interfaces:**
- Produces: `struct TexpackDevice { bool discrete, steamDeck, bc7; uint64_t deviceLocalMb; double internalScale; };` `struct TexpackChoice { uint32_t capPx; uint32_t minOriginalPx; bool on; };` `TexpackChoice AutoTexpack(const TexpackDevice&, int setting /*0 Auto,1 Off,2 1K,3 2K*/);` `uint64_t TexpackBudgetMb(uint64_t heapMb, uint64_t gameUsedMb);` (= (heap − used) / 2).

- [ ] **Step 1:** Tests, one per tier row in Global Constraints (16 GB discrete → 2048; 6 GB → 1024; 2 GB → 1024 + minOriginal 128; Steam Deck at 1x → 1024, at 2x → off; integrated at 1.5x → 1024; no BC7 → off), settings Off/1K/2K override Auto (but not `bc7=false`), budget 16000/2000 → 7000.
- [ ] **Steps 2–4:** FAIL → implement → PASS. **Step 5:** Commit.

### Task 12: Renderer integration

**Files:**
- Modify: `runtime/gpu/renderer.cpp` (texture create/reload path; memory thread; `[perf] memory:` line; teardown in `DestroyTexture`)
- Test: `tests/texpack_renderer_test.cpp` (reads `renderer.cpp`, as `depth_only_test` does: asserts the hit path is guarded by `texpack::Eligible(` and `AutoTexpack(`, and that `DestroyTexture` releases replacement memory)

**Interfaces:**
- Consumes: Tasks 2, 3, 10, 11. `Texture` gains `uint64_t packKey = 0; bool replaced = false; uint64_t replacedBytes = 0;`.

- [ ] **Step 1:** Write the source-check test → FAIL.
- [ ] **Step 2:** Implement: at startup open the pack (region from `region::`), log `[texpack] <count> textures, <tier reason>, cap <px>, budget <MB>`; on eligible (re)load compute the key; if `Has(key)` and within budget, queue `Load` on the existing memory thread and keep the original until the BC7 image is uploaded (`VK_FORMAT_BC7_UNORM_BLOCK`, own levels); swap in at the next frame boundary. **Rehash on rewrite:** a reload whose key differs drops `replaced` and the old image, logging `[texpack] rehash @<addr> <old> -> <new>` under `NFSMW_TEXPACK_LOG`. Extend the source-check test to assert the reload path compares `packKey` before reusing a replacement. LRU over replaced textures; past the budget, the least recently sampled fall back to originals.
- [ ] **Step 3:** Test PASS; build.
- [ ] **Step 4:** Runs (scripted drive, 15 min, `NFSMW_TEXPACK_LOG=1`): colour hit rate ≥ 95%; `[perf]` worst frame and memory with pack vs `NFSMW_TEXPACK=0`, no new frames > 33 ms from texture loads; `NFSMW_GPU=610M` run and `NFSMW_TEXPACK_BUDGET_MB=1024` run show tier and evictions in the log, no allocation failures; the drive log has `rehash` lines (the city streaming new art into reused memory) and no HD texture visibly on the wrong surface in screenshots taken right after them. Take 4K before/after screenshots (garage, race start, city at noon) for the user.
- [ ] **Step 5:** Commit.

### Task 13: HD Textures setting

**Files:**
- Modify: `runtime/user/settings.h` (`Id::HdTextures` after `MovieUpscaling`; `kHdTexturesAuto=0, Off=1, K1=2, K2=3`), `runtime/user/settings.cpp` (entry: Graphics, Enum, Apply::Live, env `NFSMW_HD_TEXTURES`, key `hd_textures`), `runtime/ui/settings_menu.cpp` (hidden when no pack; Auto label "Auto (2K)" like Internal Resolution), `scripts/verify_settings.py`, `README.md` settings table
- Test: extend `tests/settings_test.cpp`

- [ ] **Step 1:** Test: `hd_textures = "1k"` round-trips; unknown value → Auto; default Auto.
- [ ] **Steps 2–4:** FAIL → implement (changing the value drops replacements and reloads) → PASS; `python3 scripts/verify_settings.py` passes.
- [ ] **Step 5:** Commit.

### Task 14: AppImage packaging

**Files:**
- Modify: `scripts/linux/make_appimage.sh` (copy `texpack-extract`, `texpack-encode`, `texpack-gpuinfo`, `realesrgan-ncnn-vulkan` into `lib/texpack/`, `speedbreaker-texpack` + `hwdetect.py` + `models.json` into `texpack/`, licences), AppRun (`--texpack ...` → `lib/glibc` loader logic shared with `speedbreaker.sh` → driver)

- [ ] **Step 1:** Build the AppImage; run `SpeedBreaker-*.AppImage --texpack status` from `~` → prints pack location, count, region, model.
- [ ] **Step 2:** Check portability: `readelf -V` of each bundled tool needs no newer GLIBC/GLIBCXX than `bundle-info.txt` lists; `make_bundle.sh`'s "no game data" check also passes over `lib/texpack` and `texpack/`.
- [ ] **Step 3:** Run the game from the AppImage with the pack → `[texpack]` line shows the pack loaded.
- [ ] **Step 4:** Commit.
