#!/usr/bin/env bash
# The texture pack's third-party tools, from source, into tools/texpack/
# (git-ignored): Real-ESRGAN-ncnn-vulkan (with its pinned ncnn and libwebp)
# and bc7enc_rdo's BC7 encoder (compiled into texpack-encode, not built
# here). Model weights aren't fetched here: speedbreaker-texpack downloads
# the one it uses (texpack/models.json) on first use.
#
#   scripts/texpack/setup_tools.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/texpack/src"
REALESRGAN_COMMIT=37026f49824c5cf84062e7c6a5dd71445dcf610f   # xinntao/Real-ESRGAN-ncnn-vulkan, 2022-04-24 (v0.2.0)
BC7ENC_COMMIT=b9438627eef73a1157e84201b6fa6eb2ffd6d9f0       # richgel999/bc7enc_rdo, 2026-07-30
mkdir -p "$SRC"

if [ ! -d "$SRC/Real-ESRGAN-ncnn-vulkan" ]; then
  git clone -q https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan "$SRC/Real-ESRGAN-ncnn-vulkan"
fi
cd "$SRC/Real-ESRGAN-ncnn-vulkan"
git checkout -q "$REALESRGAN_COMMIT"
# Its libwebp submodule is listed by SSH URL: fetch it over HTTPS instead.
git -c url."https://github.com/".insteadOf=git@github.com: submodule update -q --init --recursive
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF > /dev/null
ninja -C build realesrgan-ncnn-vulkan

if [ ! -d "$SRC/bc7enc_rdo" ]; then
  git clone -q https://github.com/richgel999/bc7enc_rdo "$SRC/bc7enc_rdo"
fi
git -C "$SRC/bc7enc_rdo" checkout -q "$BC7ENC_COMMIT"

echo "texpack tools ready: $SRC/Real-ESRGAN-ncnn-vulkan/build/realesrgan-ncnn-vulkan, $SRC/bc7enc_rdo"
