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
# AppRun, a .desktop file, the icon and recomplauncher.json. Never any game data: the first run's
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

# RecompLauncher (recomplauncher.com) reads /recomplauncher.json from the
# AppImage itself: Easy Setup from the player's disc, the settings on the
# game's page, where the saves are (recomplauncher.json at the repo's top).
[ -f "$ROOT/recomplauncher.json" ] || { echo "make_appimage.sh: no recomplauncher.json in $ROOT" >&2; exit 1; }
cp "$ROOT/recomplauncher.json" "$APPDIR/recomplauncher.json"

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

# The launcher (launcher/, Qt 6 Widgets): the window before the game. Qt's
# libraries go in lib/qt and its plugins in lib/qt/plugins, used only by the
# launcher's process (AppRun runs it through the loader with that library
# path; the game never sees them). Left to the system, as the AppImage
# project's excludelist does: the C/C++ runtimes (the bundle's own decide),
# OpenGL/EGL, X11 and the core xcb library, Wayland's client libraries,
# fontconfig/freetype/harfbuzz, zlib, expat, D-Bus, systemd, Vulkan.
LAUNCHER_BIN="$BUILD/launcher/speedbreaker-launcher"
[ -x "$LAUNCHER_BIN" ] || { echo "make_appimage.sh: no $LAUNCHER_BIN (install Qt 6 Widgets, configure with -DSB_LAUNCHER=ON and build)" >&2; exit 1; }
QT_PLUGINS="$(qtpaths6 --query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/qt6/plugins)"
mkdir -p "$APPDIR/lib/launcher" "$APPDIR/lib/qt/plugins/platforms" "$APPDIR/licenses/qt"
cp "$LAUNCHER_BIN" "$APPDIR/lib/launcher/"
strip --strip-unneeded "$APPDIR/lib/launcher/speedbreaker-launcher" 2>/dev/null || true
cp "$QT_PLUGINS/platforms/libqxcb.so" "$APPDIR/lib/qt/plugins/platforms/"
[ -e "$QT_PLUGINS/platforms/libqwayland.so" ] && cp "$QT_PLUGINS/platforms/libqwayland.so" "$APPDIR/lib/qt/plugins/platforms/"
for d in xcbglintegrations wayland-shell-integration wayland-graphics-integration-client; do
    [ -d "$QT_PLUGINS/$d" ] && cp -r "$QT_PLUGINS/$d" "$APPDIR/lib/qt/plugins/"
done
if [ -e "$QT_PLUGINS/wayland-decoration-client/libbradient.so" ]; then
    mkdir -p "$APPDIR/lib/qt/plugins/wayland-decoration-client"
    cp "$QT_PLUGINS/wayland-decoration-client/libbradient.so" "$APPDIR/lib/qt/plugins/wayland-decoration-client/"
fi
host_lib() {
    case "$1" in
        libc.so.* | libm.so.* | libmvec.so.* | libpthread.so.* | libdl.so.* | librt.so.* | libresolv.so.* | ld-linux* | */ld-linux* | \
        libstdc++.so.* | libgcc_s.so.* | libGL.so.* | libEGL.so.* | libGLX.so.* | libOpenGL.so.* | libGLdispatch.so.* | \
        libdrm.so.* | libgbm.so.* | libX11.so.* | libX11-xcb.so.* | libxcb.so.1 | libXau.so.* | libXdmcp.so.* | libwayland-*.so.* | \
        libfontconfig.so.* | libfreetype.so.* | libharfbuzz.so.* | libgraphite2.so.* | libexpat.so.* | libz.so.* | \
        libdbus-1.so.* | libsystemd.so.* | libvulkan.so.*) return 0 ;;
    esac
    return 1
}
for lib in $(ldd "$APPDIR/lib/launcher/speedbreaker-launcher" $(find "$APPDIR/lib/qt/plugins" -name '*.so') | awk '/=> \// { print $1 "|" $3 }' | sort -u); do
    name="${lib%%|*}"; path="${lib#*|}"
    host_lib "$name" && continue
    [ -e "$APPDIR/lib/$name" ] || [ -e "$APPDIR/lib/qt/$name" ] || cp -L "$path" "$APPDIR/lib/qt/$name"
done
strip --strip-unneeded "$APPDIR"/lib/qt/*.so* 2>/dev/null || true
if [ -d /usr/share/licenses/qt6-base ]; then
    cp -r /usr/share/licenses/qt6-base/. "$APPDIR/licenses/qt/"
else
    echo "make_appimage.sh: no Qt licence folder (/usr/share/licenses/qt6-base)" >&2; exit 1
fi
cat > "$APPDIR/licenses/qt/README.txt" <<'EOT'
The launcher (lib/launcher/speedbreaker-launcher, SpeedBreaker's own, GPL-3.0-or-later)
uses Qt 6 (lib/qt), under the GNU LGPL version 3 (LGPL-3.0-only.txt here), dynamically
linked: replace the libraries in lib/qt with your own build of the same Qt version to use
it. Qt's source: https://download.qt.io/official_releases/qt/. The other files here are
the licences of code Qt includes. The other libraries in lib/qt come from the build
system's packages, each under its own licence.
EOT

cp "$ROOT/scripts/linux/AppRun" "$APPDIR/AppRun"
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
# The same recomplauncher.json beside it, to attach to the release too:
# RecompLauncher's catalog and checks look for it among a release's files
# (the launcher itself reads the copy inside the AppImage).
cp "$ROOT/recomplauncher.json" "$OUT/recomplauncher.json"
echo "$(du -h "$IMG" | cut -f1) $IMG (attach it, its .sha256 and recomplauncher.json to the release)"
