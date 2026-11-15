# GUI Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Qt 6 desktop launcher, opened by the AppImage before the game, that edits every in-game setting, manages save profiles, and hands off to the game on Play, falling back to the game whenever it can't run.

**Architecture:** A Qt-free core library (`launcher/core/`: settings visibility rules, evdev→HID keys, save profiles, `.sbsave` archives, backups) tested by plain ctest programs, plus a thin Qt Widgets front end (`launcher/app/`) that builds its tabs from the game's own `settings::Options()` table by compiling in `runtime/user/settings.cpp`. AppRun decides launcher vs game, runs the launcher as a child, and execs the game on the Play exit code or on any launcher failure.

**Tech Stack:** C++20, Qt 6.12 Widgets (system Qt on the build machine, bundled in the AppImage), toml++ (vendored), CMake/Ninja, bash (AppRun), ctest.

**Spec:** `docs/superpowers/specs/2026-11-15-gui-launcher-design.md`

## Global Constraints

- Linux x86_64 only; built only when `SB_LAUNCHER=ON` (default ON on Linux when `find_package(Qt6 COMPONENTS Widgets)` succeeds); the core library and its tests build without Qt.
- No SDL, no Vulkan, no game code in the launcher: only `runtime/user/settings.cpp`, `runtime/user/paths.cpp`, `runtime/platform/posix/file_io.cpp`, and header-only `runtime/user/key_names.h`, `runtime/hid/key_map.h`, `runtime/video/steam_device.h`, `runtime/user/version_check.h`.
- Writes to settings.toml only when the player changed something, and only the options already in the file plus the changed ones (`settings::SaveTouched()`); the game's `settings::Save()` is unchanged.
- A settings.toml the launcher creates (none existed) records the build's version as `[app] last_version`; an existing file's `last_version` is never touched by the launcher.
- `MigrateUserFolders()` runs before anything touches `~/.local/share/speedbreaker`.
- Every operation that replaces or removes a save profile backs it up first, to `~/.local/share/speedbreaker/save-backups/`.
- Save folders: `save` (USA), `save-pal-es`, `save-pal-de`; a `.sbsave` records its folder; import caps the archive at 64 MB.
- Exit codes: `0` window closed (stop), `10` Play (AppRun execs the game); anything else → AppRun starts the game anyway.
- AppRun: `--play` → game; `--launcher` → launcher; `--texpack …` and any other argument → as today; no arguments + Game Mode (`NFSMW_GAME_MODE=1`, else `GAMESCOPE_WAYLAND_DISPLAY` non-empty or `XDG_CURRENT_DESKTOP=gamescope`; `NFSMW_GAME_MODE=0` forces not) or no `DISPLAY` and no `WAYLAND_DISPLAY` → game without loading Qt.
- The launcher's library path reaches only the launcher process (loader `--library-path`, never an exported `LD_LIBRARY_PATH`).
- Never any game data in the AppImage or the repository; commits are local, never pushed.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; author `SpeedBreaker local <local@speedbreaker.invalid>` (no git identity is configured).

## Review Focus

1. A malicious or damaged `.sbsave` (`../`, absolute path, symlink, a second profile, a 4 GB size field, truncated header) → refused with a message, nothing written outside `save*/` → tests in Task 3.
2. A profile name that is also a prefix of another (`TJ` and `TJ2`) or contains spaces/unicode → listing, backup naming and restore pick the right one → tests in Tasks 3 and 4.
3. Opening the launcher and closing it without changes on a machine with no settings.toml → no file created; on a machine with a minimal file → file byte-identical → tests in Task 1 and Task 5.
4. Qt fails to start (no display, missing plugin) → the game still starts → Task 7 AppRun test with a launcher stub that exits 127.
5. Restore/import when the disk is full or the target folder is read-only → the existing profile is intact (extract to a temporary sibling, then swap) → test in Task 4.

---

### Task 1: `settings::SaveTouched()` and the launcher's option

**Files:**
- Modify: `runtime/user/settings.h` (declare; new `Id::LauncherShowAtStart` before `Count`)
- Modify: `runtime/user/settings.cpp` (table entry, `SaveTouched`, a masked `Serialize`)
- Test: `tests/settings_touched_test.cpp` (new), registered in `tests/CMakeLists.txt` like `settings_test` (same SOURCES and include)

**Interfaces:**
- Produces: `bool settings::SaveTouched();` — writes, atomically to the loaded path (or `DefaultPath()`), only options with `s_inFile[i] || stored != baseline`; returns true without writing when nothing is dirty; after a successful write marks those options in-file and updates their baselines. Keeps `[app]` as `Serialize` does.
- Produces: `settings::Id::LauncherShowAtStart` — table `launcher`, key `show_at_start`, `Type::Bool`, default 1, `hidden = true`, `Category::Advanced`, `Apply::Live`, env `nullptr`, label "Open the Launcher at Start", description "Show the SpeedBreaker launcher before the game starts".

- [ ] **Step 1: Write the failing tests** in `tests/settings_touched_test.cpp` (argument: a scratch directory):
  - `no_file_nothing_changed`: `Load(dir/"a.toml")` on a missing file, `SaveTouched()` → returns true, file does not exist.
  - `no_file_one_change`: missing file, `Set(Id::Scaling, <a non-default enum value>)`, `SaveTouched()` → file exists, parses, holds `[video] scaling` and no other option key (count `key = ` lines outside `[app]`: exactly 1).
  - `minimal_file_untouched`: write `"[video]\nanti_aliasing = \"off\"\n"`, `Load`, `SaveTouched()` → file bytes unchanged.
  - `minimal_file_plus_change`: same file, `Set(Id::Sharpening, 0.5)`, `SaveTouched()` → file holds `anti_aliasing` and `sharpening` and no other option; a following `Load` gives both values.
  - `launcher_option`: `Info(Id::LauncherShowAtStart)` is hidden, Bool, default 1, table `launcher`, key `show_at_start`; `Find("launcher","show_at_start")` returns it; a file with `[launcher]\nshow_at_start = false\n` loads as 0 and survives a full `Save()`.
- [ ] **Step 2: Run, expect FAIL** — `cmake --build build/main --target settings_touched_test && ctest --test-dir build/main -R settings_touched` → compile error: `SaveTouched` / `LauncherShowAtStart` not declared.
- [ ] **Step 3: Implement.** Add the Id and its table row (rows are in Id order; its own `[launcher]` table after `[game]`). Give `Serialize` a `const std::array<bool, kCount>* write` parameter (nullptr = all, for `Save()`); a table header is emitted only if one of its options is written. `SaveTouched()` shares `Save()`'s lock and write path.
- [ ] **Step 4: Run, expect PASS** — same command; then the full suite `ctest --test-dir build/main -j16` (settings_test and key_map_test must still pass: they iterate the table).
- [ ] **Step 5: Commit** — `settings: SaveTouched writes only options in the file or changed; [launcher] show_at_start`.

### Task 2: Launcher core — keys and show rules

**Files:**
- Create: `launcher/core/keys.h` (header-only), `launcher/core/rules.h`, `launcher/core/rules.cpp`
- Create: `launcher/CMakeLists.txt` (static library `sb_launcher_core`, include dirs `launcher/core` and `runtime`, sources `rules.cpp` etc.; links nothing from Qt)
- Modify: top-level `CMakeLists.txt`: `add_subdirectory(launcher)` always on Linux (the core); the Qt app inside it only when `SB_LAUNCHER`.
- Test: `tests/launcher_keys_test.cpp`, `tests/launcher_rules_test.cpp` (link `sb_launcher_core`)

**Interfaces:**
- Produces (`keys.h`, namespace `launcher::keys`): `uint16_t HidFromEvdev(uint32_t evdev)` — evdev code (Linux `KEY_*`, i.e. XKB keycode − 8) → USB HID usage, `settings::keynames::kUnknown` for anything not in `key_names.h`'s `kKeys`. `uint16_t HidFromXkb(uint32_t nativeScanCode)` = `HidFromEvdev(code - 8)` (0..7 → kUnknown).
- Produces (`rules.h`, namespace `launcher::rules`):
  - `bool SmoothMotionShown(std::span<const double> screenHz, bool steamDeck, bool optionOn)` — true if `optionOn`, else false on a Deck, else true when any rate is within 1.5 Hz of 90 or of 120.
  - `bool SteamDeck(const std::filesystem::path& dmiDir)` — reads `sys_vendor`, `product_name` (trimmed) from `dmiDir` and applies `IsSteamDeckDmi`; honours `NFSMW_HFR_DECK=1` (→ false).
  - `bool HdTexturesShown(const std::filesystem::path& userDir)` — `userDir/"texture-pack"/"pack.json"` is a regular file.
  - `bool GameRunning(const std::filesystem::path& procDir)` — some `procDir/<pid>/cmdline` contains an argument ending in `/lib/SpeedBreaker`.
  - `bool GameMode(const char* gameModeEnv, const char* gamescopeWayland, const char* xdgDesktop)` — the spec's rule (for the launcher's own refusal message; AppRun applies the same rule in bash).

- [ ] **Step 1: Write the failing tests.**
  - keys: for every `k` in `settings::keynames::kKeys`, exactly one evdev code maps to `k.code` (build the reverse by scanning 0..767); spot checks `HidFromEvdev(17 /*KEY_W*/) == 26`, `HidFromEvdev(42 /*KEY_LEFTSHIFT*/) == 225`, `HidFromEvdev(1 /*KEY_ESC*/) == 41`, `HidFromEvdev(125 /*KEY_LEFTMETA*/) == kUnknown`, `HidFromEvdev(59 /*KEY_F1*/) == kUnknown` (F1 isn't bindable), `HidFromXkb(25) == 26`.
  - rules: `SmoothMotionShown({60},false,false)==false`; `({89.9},false,false)==true`; `({119.88},false,false)==true`; `({144},false,false)==false`; `({240},false,false)==false`; `({120},true,false)==false`; `({60},false,true)==true`. `SteamDeck` on a temp dir with `Valve`/`Galileo\n` → true, `Valve`/`Steam Machine` → false, missing files → false. `HdTexturesShown` with and without `texture-pack/pack.json`. `GameRunning` on a fake proc dir: `123/cmdline` = `"/lib64/ld-linux-x86-64.so.2\0--library-path\0x\0/tmp/a/lib/SpeedBreaker\0"` → true; `"/usr/bin/SpeedBreaker-notes\0"` → false. `GameMode("1",nullptr,nullptr)` true; `("0","x","gamescope")` false; `(nullptr,"gamescope-0",nullptr)` true; `(nullptr,"","KDE")` false.
- [ ] **Step 2: Run, expect FAIL** (missing headers).
- [ ] **Step 3: Implement.** The evdev→HID table covers exactly `kKeys` (letters, digits, Enter/Esc/Backspace/Tab/Space, punctuation, F2–F24, the navigation block, the keypad, international/non-US keys, modifiers), from `<linux/input-event-codes.h>` names.
- [ ] **Step 4: Run, expect PASS**; full suite.
- [ ] **Step 5: Commit** — `launcher: core keys and visibility rules`.

### Task 3: Save profiles and the `.sbsave` archive

**Files:**
- Create: `launcher/core/saves.h`, `launcher/core/saves.cpp`, `launcher/core/sbsave.h`, `launcher/core/sbsave.cpp`
- Test: `tests/launcher_saves_test.cpp`, `tests/launcher_sbsave_test.cpp`

**Interfaces:**
- Produces (`saves.h`, namespace `launcher::saves`):
  - `inline constexpr const char* kFolders[] = {"save", "save-pal-es", "save-pal-de"};` and `const char* ReleaseLabel(std::string_view folder)` → "USA", "Europe (Spanish)", "Europe (German)".
  - `struct Profile { std::string folder; std::string name; std::string displayName; std::filesystem::file_time_type lastPlayed; };`
  - `std::vector<Profile> List(const std::filesystem::path& userDir)` — every `<folder>/<name>.xcontent` (≥ 306 bytes, `dwContentType` big-endian == 1) whose `<folder>/<name>/` directory exists; sorted by folder order then name; `lastPlayed` = newest mtime of the directory's regular files (the directory's own if empty).
  - `std::string DisplayName(std::span<const uint8_t> xcontent)` — UTF-16BE at offset 8, up to 128 units or the first 0, converted to UTF-8; "" if absent.
- Produces (`sbsave.h`, namespace `launcher::sbsave`):
  - `bool Write(const std::filesystem::path& userDir, const saves::Profile& p, const std::filesystem::path& out, std::string& error)` — ustar archive, entries `<folder>/<name>.xcontent`, `<folder>/<name>/`, `<folder>/<name>/<file>` (regular files only, one level as the game writes); written to `out + ".partial"` then renamed.
  - `struct Info { std::string folder; std::string name; uint64_t bytes = 0; };`
  - `bool Inspect(const std::filesystem::path& archive, Info& info, std::string& error)` — validates the whole archive without writing anything.
  - `bool Extract(const std::filesystem::path& archive, const std::filesystem::path& destUserDir, std::string& error)` — `Inspect` first; extracts into `destUserDir/<folder>/.import-<random>/` then is used by Task 4's swap (Extract itself never replaces an existing profile; it fails if `<folder>/<name>` exists).
  - Refusals (each with a message naming the entry): archive > 64 MB or any size field that would exceed it; truncated header/body; bad ustar checksum; absolute path; any `..` component; type other than regular file or directory; folder not in `kFolders`; entries for more than one `<folder>/<name>`; a name containing `/`, `\`, `:` or starting with `.`; missing `.xcontent` or missing directory.

- [ ] **Step 1: Write the failing tests.**
  - saves: a temp user dir with `save/TJ.xcontent` (306+ bytes, type 1, display name "TJ" UTF-16BE then 0xAAAA padding as the real file has) + `save/TJ/TJ`, `save/TJ2.xcontent` + `save/TJ2/` , `save-pal-de/Ana.xcontent` + dir, and `save/Orphan.xcontent` without a dir → `List` returns TJ, TJ2 (folder save) then Ana (save-pal-de); not Orphan; `DisplayName` of the TJ header == "TJ"; a header whose name is "Ñandú" (UTF-16BE) → "Ñandú".
  - sbsave: round trip — `Write` TJ, `Inspect` → `{folder "save", name "TJ"}`, `Extract` into a fresh user dir → identical bytes of both files. Refusals, each a hand-built archive: `../evil`, `/abs`, a symlink entry, `save/TJ/` plus `save/TJ2.xcontent`, folder `saves`, size field 4 GB, truncated after the header, wrong checksum, name `.hidden` → each `Inspect` false with a non-empty error and nothing created under the dest dir. `Extract` onto an existing `save/TJ` → false, existing files unchanged.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (own ustar reader/writer: 512-byte headers, octal sizes, checksum; no external library).
- [ ] **Step 4: Run, expect PASS**; full suite.
- [ ] **Step 5: Commit** — `launcher: save profiles and .sbsave archives`.

### Task 4: Backups, restore, import, delete

**Files:**
- Create: `launcher/core/backups.h`, `launcher/core/backups.cpp`
- Test: `tests/launcher_backups_test.cpp`

**Interfaces:**
- Consumes: Task 3's `Profile`, `List`, `sbsave::Write/Inspect/Extract`.
- Produces (namespace `launcher::backups`):
  - `std::filesystem::path Dir(const std::filesystem::path& userDir)` = `userDir/"save-backups"`.
  - `std::optional<std::filesystem::path> Backup(const std::filesystem::path& userDir, const saves::Profile& p, std::chrono::system_clock::time_point now, std::string& error)` → `save-backups/<name>-YYYYMMDD-HHMMSS.sbsave` (local time; `-2`, `-3` suffix if taken); for a non-USA folder the file name is `<name>-<folder>-YYYYMMDD-HHMMSS.sbsave`.
  - `struct Entry { std::filesystem::path path; sbsave::Info info; std::filesystem::file_time_type when; };`
  - `std::vector<Entry> List(const std::filesystem::path& userDir, const saves::Profile& p)` — backups whose `Inspect` matches `p.folder` and `p.name` exactly (not by file-name prefix), newest first.
  - `bool Replace(const std::filesystem::path& userDir, const std::filesystem::path& archive, std::chrono::system_clock::time_point now, std::string& error)` — used by Restore and Import: `Inspect`; if the profile exists, `Backup` it first (fail → stop); `Extract` to a temporary sibling; then swap (rename old dir and .xcontent aside, rename new in, remove old); any failure before the swap leaves the existing profile untouched.
  - `bool Remove(const std::filesystem::path& userDir, const saves::Profile& p, std::chrono::system_clock::time_point now, std::string& error)` — `Backup`, then delete the dir and its `.xcontent`.

- [ ] **Step 1: Write the failing tests:** backup name format at a fixed `now` (`TJ-20261115-093000.sbsave`) and the `-2` suffix on a second call in the same second; `Ana-save-pal-de-…` for PAL; `List` for `TJ` excludes a `TJ2-…` backup and a file renamed to look like `TJ-…` but holding TJ2; `Replace` of an existing TJ creates exactly one new backup and the profile's bytes equal the archive's; `Replace` with an archive that `Inspect` refuses → false, no backup, profile unchanged; `Replace` where the folder is made read-only (chmod 0555) → false, profile unchanged (skip with a note when running as root); `Remove` leaves a backup whose `Extract` restores the profile.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS**; full suite.
- [ ] **Step 5: Commit** — `launcher: backups, restore, import and delete`.

### Task 5: The Qt app — settings tabs and saving

**Files:**
- Create: `launcher/app/main.cpp`, `launcher/app/launcher_window.h/.cpp`, `launcher/app/option_row.h/.cpp`
- Modify: `launcher/CMakeLists.txt` — `if(SB_LAUNCHER)`: `find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)`, executable `speedbreaker-launcher` (AUTOMOC on) linking `sb_launcher_core`, `Qt6::Widgets`, sources `runtime/user/settings.cpp`, `runtime/user/paths.cpp`, `runtime/platform/posix/file_io.cpp`, include `tools/XenonRecomp/thirdparty/tomlplusplus/include`, definition `SB_VERSION="${CMAKE_PROJECT_VERSION}"`. Top-level: `option(SB_LAUNCHER "Build the desktop launcher (Qt 6)" ON)` on Linux, turned OFF with a message when Qt6 isn't found.
- Test: `tests/launcher_app_test.cpp` (Qt Test or plain main with `QT_QPA_PLATFORM=offscreen`, built only when `SB_LAUNCHER`), using a `LauncherWindow` constructed against a temp user dir.

**Interfaces:**
- Consumes: Task 1 `SaveTouched`, `Id::LauncherShowAtStart`; Task 2 rules.
- Produces: `class LauncherWindow : public QWidget { public: explicit LauncherWindow(std::filesystem::path userDir, QWidget* parent = nullptr); bool saveIfChanged(); QTabWidget* tabs(); signals: void playRequested(); };` and in `main.cpp` the constants `kExitClosed = 0`, `kExitPlay = 10`.
- `main.cpp` order: `MigrateUserFolders()`; `settings::Load()` (a file that doesn't parse: show the game's `LastError()` in a dialog and offer Play/Quit — never overwrite it, `SaveTouched` is not called); if `!GetBool(LauncherShowAtStart)` and no `--launcher` argument → return `kExitPlay` before creating `QApplication`; else create the window. Play → `saveIfChanged()` (if `!FileFound()` before the save, then `RecordVersion(SB_VERSION)`), return `kExitPlay`. Close → `saveIfChanged()` (same version rule), return `kExitClosed`. A failed save shows `LastError()` and keeps the window open.
- Tabs: one per `Category` in order with `CategoryName`, skipping Controls (Task 6 owns it) and any category with no shown row; rows for `!hidden` options except the rules: `HdTextures` → `HdTexturesShown`, `SmoothMotion` → `SmoothMotionShown(QScreen rates, SteamDeck("/sys/class/dmi/id"), stored != 0)`. Editors: Bool → `QCheckBox`; Enum → `QComboBox` of entry labels; Int/Float → `QSlider` on the step grid + a label showing `FormatValue`; each change calls `settings::Set`. A row whose `EnvOverride` is non-null shows "Set by <VAR> for this run" and stays editable (the file value is still stored). A help label at the bottom shows the hovered/focused row's `description`. Bottom bar: "Open this launcher at start" check box bound to `LauncherShowAtStart`, and a default "Play" button. Window title "SpeedBreaker", icon from `scripts/macos/AppIcon.png` (compiled in via a Qt resource), Fusion style, minimum 760×520. If `GameRunning("/proc")`, a banner: "SpeedBreaker is running: close it first, or its own settings will replace these when it exits."

- [ ] **Step 1: Write the failing test** (`launcher_app_test`, offscreen): with an empty temp user dir — window builds; tab titles start `Display, Graphics, Audio, Input` and include `Advanced`, exclude `Controls`; no row is labelled with a hidden option's label; HD Textures row absent, then present after creating `texture-pack/pack.json` and rebuilding; toggling the Anti-Aliasing combo to another entry then `saveIfChanged()` → settings.toml holds `anti_aliasing` and no other option, and has `last_version = "<SB_VERSION>"`; a second window with no edits → `saveIfChanged()` leaves the file byte-identical.
- [ ] **Step 2: Run, expect FAIL.** `QT_QPA_PLATFORM=offscreen ctest --test-dir build/main -R launcher_app`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS**; full suite; then run `build/main/launcher/speedbreaker-launcher --launcher` on the desktop, look at it (screenshot to the scratchpad), change Upscaling, close, and check settings.toml's diff is that one key.
- [ ] **Step 5: Commit** — `launcher: Qt window with the game's settings`.

### Task 6: Controls tab and Saves tab

**Files:**
- Create: `launcher/app/controls_tab.h/.cpp`, `launcher/app/saves_tab.h/.cpp`
- Modify: `launcher/app/launcher_window.cpp` (insert both tabs: Controls at the Controls category's position; Saves last)
- Test: extend `tests/launcher_app_test.cpp`

**Interfaces:**
- Consumes: Task 2 `keys::HidFromXkb`; Tasks 3–4 `saves::List`, `backups::*`, `sbsave::Write/Inspect`; `hid::keymap::kEntries`.
- Controls: one row per `kEntries` element: the control's label (`settings::Info(e.id).label`), two key buttons (`e.id`, `e.id2`) showing the key (Qt's `QKeySequence` name for the key when the layout gives one, else the `key_names` token with `_`→space, title case; "none" for `kNone`). Click → "Press a key…"; the next key press: Esc cancels; a key whose `HidFromXkb(nativeScanCode())` is `kUnknown`, or pressed with Meta held, is refused with "That key can't be used" in the help line and the wait goes on; otherwise `settings::Set(id, code)`. Right-click → "Clear" sets `kNone`. Under the table, for the focused row, "Also: <other controls sharing a key>" as the in-game `SharedWith` computes it. A "Reset Controls" button resets every Key option (`settings::Reset`) after a confirmation.
- Saves: a list of `saves::List(userDir)` rows: display name (folder name in grey when different), release label when more than one folder exists, "Last played <date>". Buttons: Back Up, Restore… (a menu/dialog of `backups::List` entries, newest first), Export… (`QFileDialog::getSaveFileName`, default `<name>.sbsave` in `~/`), Import… (`getOpenFileName` filtered `*.sbsave`; `Inspect`; if that profile exists, a confirmation "Replace <name>? It will be backed up first."), Delete… (confirmation "Delete <name>? A backup is kept in save-backups."), and "Open Backups Folder" (`QDesktopServices::openUrl`). Every error shows the `error` string in a message box. Empty state: "No saves yet: they appear here after you save in the game."

- [ ] **Step 1: Write the failing tests:** Controls tab has 24 rows; simulating a key press with native scan code 25 (+ text "w") on the RightTrigger first button stores 26 in `KeyRightTrigger`; native scan code 9 (Esc) leaves the value unchanged; native 133 (Super_L) leaves it unchanged and the help line contains "can't be used". Saves tab with a fixture TJ profile shows one row "TJ"; invoking the Back Up action creates one file in `save-backups/`; Delete (auto-confirmed through a test hook) removes `save/TJ` and keeps the backup.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Dialog confirmations go through one `bool confirm(const QString&)` member that the test can override.
- [ ] **Step 4: Run, expect PASS**; full suite; on the desktop: bind a key, Back Up TJ, Export to the scratchpad, Import it back (Replace), check `save/TJ/TJ` is byte-identical to before (`cmp` against a copy taken first).
- [ ] **Step 5: Commit** — `launcher: Controls and Saves tabs`.

### Task 7: AppRun and AppImage packaging

**Files:**
- Modify: `scripts/linux/make_appimage.sh`
- Test: `tests/launcher_apprun_test.sh` (bash; registered with `add_test(NAME launcher_apprun_test COMMAND bash …)`), run against a fake AppDir with stub `speedbreaker.sh` (prints `GAME $*`) and a stub launcher (exits with `$STUB_EXIT`).

**Interfaces:**
- AppRun is generated from a new checked-in file `scripts/linux/AppRun` (so the test can run it) instead of the heredoc. Layout it expects: `$HERE/speedbreaker.sh`, `$HERE/lib/launcher/speedbreaker-launcher`, Qt in `$HERE/lib/qt/` (libraries) and `$HERE/lib/qt/plugins/`.
- `pick_libs` (bash function in AppRun, shared by `--texpack` and the launcher): the same decision as `speedbreaker.sh` (glibc: bundled loader when the host's is older; libstdc++: `lib/cxx` when the host's GLIBCXX is older), output `LOADER` and `LIBS`. The launcher runs as `"$LOADER" --library-path "$LIBS:$HERE/lib/qt" "$HERE/lib/launcher/speedbreaker-launcher" "$@"` with `QT_PLUGIN_PATH="$HERE/lib/qt/plugins"` set for that command only. `--texpack` keeps working (its loader/LD path behaviour moves into `pick_libs`; `speedbreaker-texpack` still gets `SB_TEXPACK_LOADER`/`SB_TEXPACK_LIBS`).
- make_appimage.sh: requires `$BUILD/launcher/speedbreaker-launcher` (clear error if missing: "configure with -DSB_LAUNCHER=ON"); copies Qt libraries found by `ldd` into `lib/qt/` except the host list: glibc family, `libstdc++`, `libgcc_s`, `libGL*`, `libEGL`, `libOpenGL`, `libGLX`, `libGLdispatch`, `libdrm`, `libgbm`, `libX11*`, `libxcb.so.1`, `libXau`, `libXdmcp`, `libwayland-*`, `libfontconfig`, `libfreetype`, `libharfbuzz`, `libgraphite2`, `libexpat`, `libz`, `libdbus-1`, `libsystemd`, `libvulkan`; copies plugins `platforms/libqxcb.so`, `platforms/libqwayland*.so` (if present), `xcbglintegrations/`, `wayland-*` plugin dirs if present, and `ldd`s them too (xcb-cursor, xcb-icccm, -image, -keysyms, -render-util, xkbcommon-x11 get bundled by the same rule); Qt's licence (`/usr/share/licenses/qt6-base/LICENSE*` or the LGPL-3.0 text) to `licenses/qt/`, with a README line in `licenses/`.

- [ ] **Step 1: Write the failing test** `tests/launcher_apprun_test.sh` cases (env cleared with `env -i HOME=$tmp PATH=$PATH`):
  - `--play` → output `GAME ` and the stub launcher not run.
  - no args, `DISPLAY=:9`, stub exit 10 → `GAME `; exit 0 → no `GAME`, exit status 0; exit 127 → `GAME ` (fallback); exit 139 → `GAME `.
  - no args, `DISPLAY=:9 XDG_CURRENT_DESKTOP=gamescope` → `GAME `, launcher not run; `NFSMW_GAME_MODE=0` with gamescope → launcher run.
  - no args, no `DISPLAY`/`WAYLAND_DISPLAY` → `GAME `, launcher not run.
  - `--launcher` with `show_at_start = false` irrelevant to AppRun (the launcher decides) → launcher run.
  - `somefile.iso` → `GAME somefile.iso`.
  - the game's environment never contains `LD_LIBRARY_PATH` or `QT_PLUGIN_PATH` set by AppRun (stub prints `env`).
- [ ] **Step 2: Run, expect FAIL** (no `scripts/linux/AppRun`).
- [ ] **Step 3: Implement** AppRun and the make_appimage.sh changes.
- [ ] **Step 4: Run, expect PASS**; full suite; build the AppImage (`scripts/linux/make_appimage.sh tools/appimage-runtime-x86_64`), check its size is reported and `ldd`-check `lib/launcher/speedbreaker-launcher` inside the extracted AppDir under the bundled loader finds every library (`"$LOADER" --library-path … --list …`).
- [ ] **Step 5: Commit** — `appimage: the launcher opens first, the game starts on Play or whenever the launcher can't`.

### Task 8: End-to-end on this machine

**Files:** none new (fixes go to the task that owns the code, with a test).

- [ ] **Step 1:** Install the new AppImage over `~/.local/bin/SpeedBreaker-0.2.5-x86_64.AppImage` (keep the old one as `.bak` until this task passes).
- [ ] **Step 2:** Run it with no arguments: the launcher window opens (screenshot); change Upscaling to FSR 1 and one key; Play → the game starts; its log (`~/.local/share/speedbreaker/last-run.log`) shows the new upscaling and the game window appears; quit the game. `git diff`-style compare settings.toml before/after: only those two keys plus nothing else changed.
- [ ] **Step 3:** `--play` skips the window; `--texpack status` still prints the pack; untick "Open this launcher at start", run with no args → the game starts directly; `--launcher` brings the window back; tick it again.
- [ ] **Step 4:** Fallback: run the AppImage with `QT_QPA_PLATFORM=nonexistent` → the launcher fails and the game starts.
- [ ] **Step 5:** Saves round trip on a copy of the user dir (`HOME=<scratch copy>`): Back Up, Export, Delete, Import → TJ byte-identical to the original.
- [ ] **Step 6:** Record results in the ledger; remove the `.bak` AppImage only when all pass. Commit nothing unless a fix was needed.
