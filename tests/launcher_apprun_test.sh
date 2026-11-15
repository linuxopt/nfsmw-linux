#!/bin/bash
# The AppImage's AppRun (scripts/linux/AppRun): which of the launcher and the
# game it starts, and that the game starts whenever the launcher can't, on a
# fake AppDir with a stub game (prints what it got) and a stub launcher
# (tests/launcher_stub.cpp, exits with $STUB_EXIT).
#   launcher_apprun_test.sh <repo root> <stub launcher> <scratch folder>
set -u
ROOT="$1" STUB="$2" SCRATCH="$3"
failures=0
fail() { echo "  FAIL: $*"; failures=$((failures + 1)); }

rm -rf "$SCRATCH"
APPDIR="$SCRATCH/AppDir"
mkdir -p "$APPDIR/lib/launcher" "$SCRATCH/home"
cp "$ROOT/scripts/linux/AppRun" "$APPDIR/AppRun" || { echo "no scripts/linux/AppRun"; exit 1; }
chmod 755 "$APPDIR/AppRun"
cp "$STUB" "$APPDIR/lib/launcher/speedbreaker-launcher"
cat > "$APPDIR/speedbreaker.sh" <<'EOF'
#!/bin/bash
echo "GAME $*"
echo "GAMEENV LD=${LD_LIBRARY_PATH-unset} QT=${QT_PLUGIN_PATH-unset}"
EOF
chmod 755 "$APPDIR/speedbreaker.sh"

# Runs AppRun with a clean environment plus the given VAR=value words.
run() {
    local vars=()
    while [ $# -gt 0 ] && [[ "$1" == *=* ]]; do vars+=("$1"); shift; done
    env -i HOME="$SCRATCH/home" PATH="$PATH" "${vars[@]}" "$APPDIR/AppRun" "$@" 2>/dev/null
}
has() { grep -qx -- "$2" <<< "$1"; }
lacks() { ! grep -q -- "$2" <<< "$1"; }

out="$(run DISPLAY=:9 --play)"
has "$out" "GAME " && lacks "$out" LAUNCHER || fail "--play: the game, no launcher: $out"

out="$(run DISPLAY=:9 STUB_EXIT=10)"
has "$out" "LAUNCHER" && has "$out" "GAME " || fail "Play (10): launcher then game: $out"
has "$out" "LAUNCHERENV QT=unset SB=set" || fail "the launcher gets Qt's plugin folder privately (not QT_PLUGIN_PATH, which programs it opens would inherit): $out"
has "$out" "GAMEENV LD=unset QT=unset" || fail "the game gets neither LD_LIBRARY_PATH nor QT_PLUGIN_PATH: $out"

out="$(run DISPLAY=:9 STUB_EXIT=0)"; status=$?
has "$out" "LAUNCHER" && lacks "$out" "GAME" && [ "$status" = 0 ] || fail "closed (0): no game, status 0: $out ($status)"

out="$(run DISPLAY=:9 STUB_EXIT=127)"
has "$out" "GAME " || fail "a launcher that can't start (127): the game anyway: $out"
out="$(run DISPLAY=:9 STUB_EXIT=139)"
has "$out" "GAME " || fail "a launcher that crashed (139): the game anyway: $out"

rm -f "$APPDIR/lib/launcher/speedbreaker-launcher"
out="$(run DISPLAY=:9)"
has "$out" "GAME " || fail "no launcher in the AppImage: the game: $out"
cp "$STUB" "$APPDIR/lib/launcher/speedbreaker-launcher"

out="$(run DISPLAY=:9 XDG_CURRENT_DESKTOP=gamescope STUB_EXIT=0)"
has "$out" "GAME " && lacks "$out" LAUNCHER || fail "Game Mode (gamescope desktop): the game, no launcher: $out"
out="$(run DISPLAY=:9 GAMESCOPE_WAYLAND_DISPLAY=gamescope-0 STUB_EXIT=0)"
has "$out" "GAME " && lacks "$out" LAUNCHER || fail "Game Mode (gamescope display): the game: $out"
out="$(run DISPLAY=:9 XDG_CURRENT_DESKTOP=gamescope NFSMW_GAME_MODE=0 STUB_EXIT=0)"
has "$out" "LAUNCHER" && lacks "$out" "GAME" || fail "NFSMW_GAME_MODE=0: the launcher: $out"
out="$(run DISPLAY=:9 NFSMW_GAME_MODE=1 STUB_EXIT=0)"
has "$out" "GAME " && lacks "$out" LAUNCHER || fail "NFSMW_GAME_MODE=1: the game: $out"

out="$(run STUB_EXIT=0)"
has "$out" "GAME " && lacks "$out" LAUNCHER || fail "no display: the game: $out"
out="$(run WAYLAND_DISPLAY=wayland-0 STUB_EXIT=0)"
has "$out" "LAUNCHER" || fail "a Wayland display: the launcher: $out"

out="$(run XDG_CURRENT_DESKTOP=gamescope STUB_EXIT=0 --launcher)"
has "$out" "LAUNCHER --launcher" || fail "--launcher: the launcher, told it was asked for: $out"

out="$(run DISPLAY=:9 STUB_EXIT=0 somefile.iso)"
has "$out" "GAME somefile.iso" && lacks "$out" LAUNCHER || fail "a file: the game with it: $out"

if [ "$failures" = 0 ]; then
    echo "launcher_apprun_test: all passed"
else
    echo "launcher_apprun_test: $failures failure(s)"
    exit 1
fi
