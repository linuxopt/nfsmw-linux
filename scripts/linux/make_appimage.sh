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

# The texture pack tools (texpack/, scripts/texpack/setup_tools.sh): the extractor,
# encoder and GPU probe from the build, Real-ESRGAN-ncnn-vulkan from tools/, and
# the driver (Python 3, stdlib only). Never any game data or model weights.
BUILD="$(readlink -f "${SB_BUILD:-$ROOT/build/main}")"
RESR="$ROOT/tools/texpack/src/Real-ESRGAN-ncnn-vulkan/build/realesrgan-ncnn-vulkan"
mkdir -p "$APPDIR/lib/texpack" "$APPDIR/texpack" "$APPDIR/licenses/texpack"
for t in texpack-extract texpack-encode texpack-gpuinfo; do
    [ -x "$BUILD/texpack/$t" ] || { echo "make_appimage.sh: no $BUILD/texpack/$t (configure with -DSB_TEXPACK=ON and build)" >&2; exit 1; }
    cp "$BUILD/texpack/$t" "$APPDIR/lib/texpack/"
done
[ -x "$RESR" ] || { echo "make_appimage.sh: no $RESR (scripts/texpack/setup_tools.sh)" >&2; exit 1; }
cp "$RESR" "$APPDIR/lib/texpack/"
strip --strip-unneeded "$APPDIR"/lib/texpack/* 2>/dev/null || true
# Libraries the tools need beyond the bundle's (libomp for the upscaler's threads).
for lib in $(ldd "$APPDIR"/lib/texpack/* | awk '/=> \// { print $1 "|" $3 }' | sort -u); do
    name="${lib%%|*}"; path="${lib#*|}"
    case "$name" in
        libvulkan.so.* | libc.so.* | libm.so.* | libmvec.so.* | libpthread.so.* | libdl.so.* | librt.so.* | ld-linux* | */ld-linux* | libstdc++.so.* | libgcc_s.so.*) ;;
        *) [ -e "$APPDIR/lib/$name" ] || cp -L "$path" "$APPDIR/lib/$name" ;;
    esac
done
cp "$ROOT/texpack/speedbreaker-texpack" "$ROOT/texpack/hwdetect.py" "$ROOT/texpack/models.json" "$APPDIR/texpack/"
chmod 755 "$APPDIR/texpack/speedbreaker-texpack"
lic() { [ -f "$2" ] || { echo "make_appimage.sh: no licence file $2" >&2; exit 1; }; cp "$2" "$APPDIR/licenses/texpack/$1"; }
lic Real-ESRGAN-ncnn-vulkan-LICENSE "$ROOT/tools/texpack/src/Real-ESRGAN-ncnn-vulkan/LICENSE"
lic ncnn-LICENSE.txt "$ROOT/tools/texpack/src/Real-ESRGAN-ncnn-vulkan/src/ncnn/LICENSE.txt"
lic libwebp-COPYING "$ROOT/tools/texpack/src/Real-ESRGAN-ncnn-vulkan/src/libwebp/COPYING"
lic openmp-LICENSE /usr/share/licenses/openmp/LICENSE
lic bc7enc-LICENSE "$ROOT/tools/texpack/src/bc7enc_rdo/LICENSE"
lic stb-MIT.txt "$ROOT/LICENSES/stb-MIT.txt"
lic xxHash-BSD-2-Clause.txt "$ROOT/LICENSES/xxHash-BSD-2-Clause.txt"
cat > "$APPDIR/licenses/texpack/README.txt" <<'EOT'
The HD texture pack tools in lib/texpack and texpack/ (built from SpeedBreaker's texpack/):
  realesrgan-ncnn-vulkan: Real-ESRGAN-ncnn-vulkan (BSD-3-Clause), with ncnn (BSD-3-Clause) and libwebp (BSD-3-Clause)
  libomp (lib/): LLVM OpenMP runtime (Apache-2.0 WITH LLVM-exception)
  texpack-encode: bc7enc (MIT or public domain), stb_image and stb_image_write (MIT or public domain)
  texpack-*: xxHash (BSD-2-Clause)
The AI model's weights are not included: speedbreaker-texpack downloads the one
it uses on first use (texpack/models.json lists each one's licence).
EOT

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
# SpeedBreaker AppImage entry point: hands off to the bundle's own launcher,
# which picks the bundled or system glibc/libstdc++ and runs lib/SpeedBreaker.
# Settings, saves, logs and the installed game live in ~/.local/share/speedbreaker
# (the AppImage is read-only, so portable mode is not available).
#
#   SpeedBreaker.AppImage --texpack build|status|remove   the HD texture pack
#   SpeedBreaker.AppImage --texpack capture               play with play-capture on
HERE="$(dirname "$(readlink -f "$0")")"
if [ "${1:-}" = "--texpack" ]; then
    shift
    if [ "${1:-}" = capture ]; then
        shift
        NFSMW_TEXPACK_CAPTURE=1 exec "$HERE/speedbreaker.sh" "$@"
    fi
    command -v python3 > /dev/null || { echo "speedbreaker-texpack needs Python 3 (python3), which this system lacks" >&2; exit 1; }
    # The tools were built against a newer C library than some systems have: run them
    # through the bundled one when the system's is older (as speedbreaker.sh does).
    v() { "$1" --version 2>/dev/null | sed -n '1s/.* version \([0-9][0-9.]*[0-9]\).*/\1/p'; }
    host="$(v /lib64/ld-linux-x86-64.so.2)" bundled="$(v "$HERE/lib/glibc/ld-linux-x86-64.so.2")"
    LIBS="$HERE/lib"
    if [ -n "$bundled" ] && { [ -z "$host" ] || [ "$(printf '%s\n%s\n' "$host" "$bundled" | sort -V | head -1)" != "$bundled" ]; }; then
        export SB_TEXPACK_LOADER="$HERE/lib/glibc/ld-linux-x86-64.so.2" SB_TEXPACK_LIBS="$LIBS:$HERE/lib/cxx:$HERE/lib/glibc"
    else
        export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi
    export SB_TEXPACK_BIN="$HERE/lib/texpack"
    exec python3 -B "$HERE/texpack/speedbreaker-texpack" "$@"
fi
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
