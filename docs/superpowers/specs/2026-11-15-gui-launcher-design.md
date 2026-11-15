# SpeedBreaker GUI launcher: design

Status: approved in conversation 2026-11-15 ("nothing wrong or missing. continue the build").

## 1. Intent

The player wants one desktop window that opens before the game, where they change
graphics (FSR upscaling included), controls and their save before pressing Play:
"just add a GUI launcher so i can use to edit game before starting the game".
The game is shared across several Linux machines through one AppImage, so the
launcher has to work on each without anything installed beyond what the
AppImage already needs.

Success: opening the AppImage shows the launcher; every option the in-game
Settings menu has can be changed there (Wheel > Calibrate excepted: it is an
action that reads the live wheel, and stays in-game); Play starts the game
with those settings; a save can be backed up, restored and carried to another
machine as one file; and a machine where the launcher can't start still
starts the game.

## 2. Goals, non-goals, anti-goals

Goals
- A Qt 6 desktop window: tabs for each settings category, a Saves tab, a Play
  button, and an "Open this launcher at start" box.
- Settings come from the game's own option table, so nothing is listed twice.
- Saves: list profiles, back up, restore a backup, export and import one file,
  delete (with a confirmation).
- Shipped inside the existing AppImage, Qt included.

Non-goals (this project)
- Mods and DLC (the game has no mod loader; nothing registers DLC with the
  game). Asked for at first, then left out by the player.
- Editing values inside a save (cash, bounty, cars): the save format and its
  checksum aren't decoded. Later, separately.
- Building the HD texture pack from the launcher (the CLI `--texpack build`
  stays the way).
- Windows, macOS, iOS: Linux only.

Anti-goals
- A second copy of the settings list (labels, ranges, defaults) in the
  launcher. It would drift from the game; that was the stated risk of this
  approach and the design removes it.
- Losing a save: every operation that replaces or removes a profile backs it
  up first.
- Getting in the way of Steam / SteamOS Game Mode, where a mouse-driven window
  can't be used.
- A corrupted settings.toml: writes stay atomic, as the game's are.

## 3. Constraints

- Linux x86_64, inside `SpeedBreaker-<version>-x86_64.AppImage`
  (scripts/linux/make_appimage.sh). The AppImage must keep running on hosts with
  an older glibc (AppRun already runs bundled tools through
  `lib/glibc/ld-linux-x86-64.so.2` when the host's is older; the launcher goes
  the same way).
- Qt is bundled (the other machines may not have it). Expected AppImage growth:
  roughly 25-40 MB compressed (Qt Core/Gui/Widgets, the xcb platform plugin,
  and ICU, which Arch's Qt Core links).
- GPL-3.0-or-later, like the rest of SpeedBreaker. Qt is LGPL-3.0: dynamically
  linked, its licence text shipped in `licenses/`.
- The user's data stays where it is: `~/.local/share/speedbreaker`
  (`settings.toml`, `save/`, `game/`). The AppImage is read-only.
- Never any game data in the AppImage or the repository.

## 4. Approaches

A. A launcher screen inside the game binary (ImGui, like the first-run
   installer screen, `ui::RunInstaller` in runtime/main.cpp), reusing
   ui/settings_menu.cpp. Every option and key capture for free, one binary; but
   it looks like the game's menus, not a desktop app.
B. **A separate desktop app (chosen by the player).** A Qt 6 program that edits
   settings.toml. Looks like a normal Linux application. Its usual cost, a
   second list of settings that drifts, is removed by compiling in the game's
   own settings code (section 5.2).
C. A separate SDL3 + ImGui program built from the game's menu code. Own window,
   but the menu is tangled with the renderer (it asks the renderer, the
   presenter and the wheel code what to show); more work than A for little gain.

The player picked B.

## 5. Design

### 5.1 Parts

- `launcher/` (new, CMake option `SB_LAUNCHER`, default ON on Linux when Qt 6
  Widgets is found): executable `speedbreaker-launcher`, C++20, Qt 6 Widgets.
- Links `runtime/user/settings.cpp` and `runtime/platform/posix/file_io.cpp`
  with toml++ (`tools/XenonRecomp/thirdparty/tomlplusplus/include`), exactly
  as `tests/settings_test` does today. No SDL, no Vulkan, no game code.
- AppImage: the binary in `lib/launcher/` (or `lib/`), Qt's libraries in
  `lib/`, its plugins under `lib/qt6/plugins`, a `qt.conf` pointing there.

### 5.2 Settings (decided: the game's own table)

- The launcher calls `settings::Load()` and iterates `settings::Options()`;
  each `Category` (Display, Graphics, Audio, Input, Wheel, Controls, Game,
  Advanced, in that order, with `settings::CategoryName`) becomes a tab, each
  non-hidden option a row: label left, editor right, and the option's
  `description` in a help line at the bottom for the hovered or focused row
  (as sketched to the player).
- Editors by `Type`: Bool → check box; Enum → combo box of the entries'
  labels; Int / Float → slider or spin box over `min..max` in `step`, shown with
  `unit` the way the game formats it (`settings::FormatValue` if exported, else
  the same rule); Key → a button showing the key, click then press a key to
  bind (5.4).
- Before anything touches the data folder, the launcher calls
  `MigrateUserFolders()` (runtime/user/paths.cpp), as the game does, so a
  machine still on the old `nfsmw-recomp` folder isn't left with two
  (creating `~/.local/share/speedbreaker` first would strand the old one's
  saves and game, paths.cpp:22-25).
- Writes are atomic (temp file, fsync, rename), as `settings::Save()`'s are,
  and happen only when the player changed something (the game's own rule,
  `Dirty()`, main.cpp:717). They must not pin defaults: today `Save()` writes
  every option into the file (settings.cpp:1769-1791), which would fix a PAL
  player's language to English (the game sets that default from the disc
  before loading, main.cpp:295-310) and freeze every default in place. So the
  launcher writes only the options already in the file plus the ones the
  player changed: a new function in settings.cpp (e.g. `SaveTouched()`), with
  the game's `Save()` unchanged. A settings.toml the launcher creates before
  the game's first run must not make the game take a new player for an
  updated one (no `last_version` reads as `Unversioned` and shows "What's
  new", user/version_check.h:70-74, 96-101): the launcher writes the
  current version's `last_version` when it creates the file, or the builder
  finds an equivalent.
- `hidden` already hides what other platforms use (iPhone's Present Scale,
  macOS HDR) because settings.cpp is compiled for Linux. Rows the in-game menu
  shows conditionally (ui/settings_menu.cpp `Shown` and `PageShown`) get Qt
  equivalents:
  - HD Textures: only when a pack is installed
    (`~/.local/share/speedbreaker/texture-pack/pack.json` exists). The game
    also requires the pack's region to match the installed release
    (gpu/renderer.cpp:4629-4632); the launcher doesn't check that (a
    mismatched pack's setting simply has no effect in-game).
  - Smooth Motion: mirrors the game's Linux rule (presenter.cpp:6278-6303,
    refresh_multiple.h:96-101) as far as the launcher can see it: a screen at
    about 90 Hz (±1.5) or about 120 Hz (twice 60), never on a Steam Deck; or
    the option is on.
  - Wheel tab: always shown (the player may set it up before plugging the
    wheel in). Its Calibrate action is not in the launcher (needs the live
    wheel); its results are hidden options, kept as they are.
  - HDR Output, Strength and Paper White are visible on Linux, as in-game,
    without the in-game live "In use: ..." status.
  - Game tab: as in-game, hidden when its only row is hidden.
- An environment variable that overrides an option (`Option::env`, e.g.
  `NFSMW_AA`) is passed on to the game by the launcher, so the row shows that
  it is set by that variable, as the in-game menu does.
- `Apply::Restart` needs no marker: the game isn't running.
- A new hidden option for the launcher's own box: `[launcher] show_at_start`,
  Bool, default on, added to the table (the game ignores it; it has to live in
  the table so the game's Save doesn't drop it).

### 5.3 Saves tab

- Profiles are the pairs `<folder>/<name>/` + `<folder>/<name>.xcontent`
  (runtime/kernel/xam.cpp `ContentRoot`, `ScanSavedContent`), where each
  release has its own folder: `save/` (USA), `save-pal-es/`, `save-pal-de/`
  (runtime/user/paths.h:163-169). The launcher doesn't work out which release
  is installed (that hashes default.xex); it lists the profiles of every
  folder that exists, labelled by release when more than one does. The
  display name is the `.xcontent`'s `szDisplayName` when present, else the
  folder name. Last played: the newest modification time inside the folder.
- Backups, exports and imports keep the release: a `.sbsave` records its
  folder (`save`, `save-pal-es`, `save-pal-de`), and import puts it back in
  that folder.
- Back up: writes one archive to `~/.local/share/speedbreaker/save-backups/`
  named `<name>-YYYYMMDD-HHMMSS.sbsave`.
- Restore: pick a backup of that profile; the current one is backed up first.
- Export: save one `.sbsave` file where the player chooses (file dialog).
- Import: open a `.sbsave`; if a profile of that name exists, ask to replace,
  backing the old one up first.
- Delete: confirm, back up, then remove.
- `.sbsave` format (builder's call within this): a plain ustar tar of
  `<name>.xcontent` and `<name>/…`, written and read by the launcher's own code.
  Import refuses absolute paths, `..`, links and anything outside those two
  entries, and caps the total size (saves are kilobytes; 64 MB cap).

### 5.4 Key capture

- Stored keys are USB HID usages (= SDL scancodes), named by
  `runtime/user/key_names.h` ("w", "left_shift", "none").
- On Linux, `QKeyEvent::nativeScanCode()` is the XKB keycode = evdev code + 8.
  The launcher maps evdev codes to HID usages with its own table, for exactly the
  keys `key_names.h` lists (a key outside the table is refused, as in-game).
  A test checks every `key_names.h` entry has a mapping.
- Labels: the key_names token made readable ("Left Shift"), or Qt's name of the
  key; builder's call.
- Esc cancels a capture; the in-game rules for clearing a key and for a key
  already bound elsewhere apply (read ui/settings_menu.cpp and mirror them).

### 5.5 Starting, and Play

- AppRun decides, and the game is always started by AppRun, never by the
  launcher (so the launcher's library path never reaches the game, whose
  `speedbreaker.sh` deliberately avoids `LD_LIBRARY_PATH`,
  scripts/steamos/bundle/speedbreaker.sh:26-28, 140-175):
  - `--play` → the game. `--launcher` → the launcher. Any other argument
    (`--texpack …`, files, installer flags) → as today.
  - No arguments, in SteamOS Game Mode (the game's own rule,
    video/presenter.cpp `DetectGameMode`: `NFSMW_GAME_MODE`, else
    `GAMESCOPE_WAYLAND_DISPLAY` set or `XDG_CURRENT_DESKTOP=gamescope`), or
    with neither `DISPLAY` nor `WAYLAND_DISPLAY` → the game, without loading
    Qt.
  - Otherwise AppRun runs the launcher as a child and waits. Exit code 0:
    the window was closed, stop. A dedicated code (e.g. 10): Play, so AppRun
    execs `speedbreaker.sh`. Any other code (a missing library, a crash, Qt
    unable to open a display) → AppRun starts the game anyway, so a machine
    where the launcher can't run still plays.
  - `show_at_start` off: the launcher reads settings and returns the Play
    code at once, before creating a window (and if that fails, the fallback
    above still starts the game).
- Play saves settings (5.2), then exits with the Play code.
- If the game isn't installed yet, Play still starts it, and its first-run
  installer runs as today; the Saves tab is empty until then.
- If a SpeedBreaker game process is already running, the launcher says so
  (its settings would be overwritten by the game's on exit).

### 5.6 Look

- Qt Fusion style, following the system's light or dark scheme; the
  SpeedBreaker icon and window title; a minimum size around 760 x 520.
- Layout as shown to the player:

```
┌ SpeedBreaker ──────────────────────────────────────────┐
│ [Display] [Graphics] [Audio] [Controls] [Saves] ...    │
│  Upscaling        [ FSR 1                  ▾]          │
│  ...                                                   │
│  <help line for the hovered row>                       │
│ ☑ Open this launcher at start          [   PLAY   ]    │
└────────────────────────────────────────────────────────┘
```

### 5.7 Packaging

- Target machines (builder's call, made here): current Arch-family desktops
  (X11 or Wayland) and SteamOS Desktop Mode; anything older or odder is
  covered by the fallback in 5.5, which starts the game when the launcher
  can't.
- make_appimage.sh copies `speedbreaker-launcher`, the Qt libraries it needs
  (by ldd, with the same exclusions as the texpack tools plus the libraries the
  AppImage project's excludelist leaves to the host: libGL/libEGL, libX11,
  libxcb, fontconfig, freetype, libdrm...), the xcb platform plugin with the
  xcb-util libraries it needs (xcb-cursor, -icccm, -image, -keysyms,
  -render-util) and libxkbcommon(-x11), the wayland platform plugin when Qt
  has it, and Qt's licence.
- AppRun's glibc check becomes a shared function used by both `--texpack` and
  the launcher; for the launcher it also puts `lib/cxx` on the path when the
  host's libstdc++ is older than the build's (as speedbreaker.sh decides). The
  library path is set only for the launcher's process.
- The `.desktop` entry stays `Exec=speedbreaker %F` (opens the launcher).

## 6. Left to the builder

- File layout inside `launcher/`, class structure, whether the model behind
  the tabs is a Qt model or plain widgets.
- Exact widgets for Int/Float rows; whether to save on every change.
- How to test: the non-GUI parts (launch decision, profile list, .sbsave
  write/read and its refusals, evdev→HID table, show rules) as plain unit tests;
  widgets under `QT_QPA_PLATFORM=offscreen`. Registered with ctest like the
  others.
- Proving it in the AppImage on this machine: the launcher opens, a changed
  setting reaches the game (the game's log or settings.toml), Play starts the
  game, export/import round-trips the TJ profile on a copy.
