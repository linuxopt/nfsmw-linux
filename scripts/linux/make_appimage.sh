#!/bin/bash
# SpeedBreaker as a single-file Linux AppImage, built from the SteamOS bundle.
#
#   scripts/steamos/make_bundle.sh build/main build/bundle   # first (SB_UNTAGGED=1 for a dev build)
#   scripts/linux/make_appimage.sh <runtime> [bundle dir] [output dir]
#
# <runtime> is an AppImage type-2 runtime stub (runtime-x86_64 from
# github.com/AppImage/type2-runtime, or the first <offset> bytes of any type-2
# AppImage, see --appimage-offset). Needs mksquashfs (squashfs-tools).
#
# The AppImage holds exactly the bundle (launcher, lib/, licenses) plus
# AppRun, a .desktop file and the icon. Never any game data: the first run's
# installer takes the player's own disc image, into ~/.local/share/speedbreaker.
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
RUNTIME="${1:?usage: make_appimage.sh <type2 runtime> [bundle dir] [output dir]}"
BUNDLE="$(readlink -f "${2:-$ROOT/build/bundle/SpeedBreaker}")"
OUT="$(readlink -f "${3:-$ROOT/build/appimage}")"
command -v mksquashfs >/dev/null || { echo "make_appimage.sh: no mksquashfs (install squashfs-tools)" >&2; exit 1; }
[ -x "$BUNDLE/speedbreaker.sh" ] && [ -x "$BUNDLE/lib/SpeedBreaker" ] || { echo "make_appimage.sh: $BUNDLE isn't a bundle (run make_bundle.sh first)" >&2; exit 1; }
[ "$(head -c 4 "$RUNTIME" | od -An -c | tr -d ' ')" = '177ELF' ] || { echo "make_appimage.sh: $RUNTIME isn't an ELF runtime" >&2; exit 1; }

VERSION="$(sed -n 's/^SpeedBreaker v\([0-9.]*\).*/\1/p' "$BUNDLE/VERSION")"
APPDIR="$OUT/SpeedBreaker.AppDir"
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
cp -a "$BUNDLE/." "$APPDIR/"
rm -f "$APPDIR"/*.log

mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/applications"
if command -v magick >/dev/null; then
    magick "$ROOT/scripts/macos/AppIcon.png" -resize 256x256 "$APPDIR/speedbreaker.png"
else
    python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).resize((256,256), Image.LANCZOS).save(sys.argv[2])' \
        "$ROOT/scripts/macos/AppIcon.png" "$APPDIR/speedbreaker.png"
fi
cp "$APPDIR/speedbreaker.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
ln -s speedbreaker.png "$APPDIR/.DirIcon"

cat > "$APPDIR/speedbreaker.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=SpeedBreaker
GenericName=Need for Speed: Most Wanted (Xbox 360) native port
Comment=Static recompilation of NFS Most Wanted (2005, Xbox 360). Bring your own disc image.
Exec=speedbreaker %F
Icon=speedbreaker
Categories=Game;ActionGame;
Keywords=nfs;racing;mostwanted;recomp;
Terminal=false
StartupWMClass=SpeedBreaker
X-AppImage-Version=$VERSION
EOF
cp "$APPDIR/speedbreaker.desktop" "$APPDIR/usr/share/applications/"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
# SpeedBreaker AppImage entry point: hands off to the bundle's own launcher,
# which picks the bundled or system glibc/libstdc++ and runs lib/SpeedBreaker.
# Settings, saves, logs and the installed game live in ~/.local/share/speedbreaker
# (the AppImage is read-only, so portable mode is not available).
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/speedbreaker.sh" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

IMG="$OUT/SpeedBreaker-$VERSION-x86_64.AppImage"
SQ="$OUT/.squashfs.partial"
rm -f "$SQ"
mksquashfs "$APPDIR" "$SQ" -root-owned -noappend -comp zstd -Xcompression-level 19 -b 1M -quiet
cat "$RUNTIME" "$SQ" > "$IMG.partial"
rm -f "$SQ"
chmod 755 "$IMG.partial"
mv "$IMG.partial" "$IMG"
(cd "$OUT" && sha256sum "$(basename "$IMG")" > "$(basename "$IMG").sha256")
echo "$(du -h "$IMG" | cut -f1) $IMG"
