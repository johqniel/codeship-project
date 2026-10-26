#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WIDTH="${1:-1280}"
HEIGHT="${2:-720}"
FPS="${3:-30}"
MODE="${4:-render}"
COMPUTE="${5:-CUDA}"
PROJECT="${6:-Fractal2DDemo}"

if [[ ! "${WIDTH}" =~ ^[0-9]+$ || ! "${HEIGHT}" =~ ^[0-9]+$ || ! "${FPS}" =~ ^[0-9]+$ ]]; then
  echo "Usage: $0 [width] [height] [fps] [render|smoketest]" >&2
  exit 1
fi

if [[ "${MODE}" != "render" && "${MODE}" != "smoketest" ]]; then
  echo "Error: mode must be 'render' or 'smoketest'." >&2
  echo "Usage: $0 [width] [height] [fps] [render|smoketest] [CUDA|HIP|auto] [project]" >&2
  exit 1
fi

if [[ "${COMPUTE}" != "CUDA" && "${COMPUTE}" != "HIP" && "${COMPUTE}" != "auto" ]]; then
  echo "Error: compute backend must be one of: CUDA, HIP, auto." >&2
  echo "Usage: $0 [width] [height] [fps] [render|smoketest] [CUDA|HIP|auto] [project]" >&2
  exit 1
fi

cd "${REPO_ROOT}"

FFMPEG_DEV_PATH="$(nix path-info nixpkgs#ffmpeg^dev)"
GLIB_DEV_PATH="$(nix path-info nixpkgs#glib^dev)"
CAIRO_DEV_PATH="$(nix path-info nixpkgs#cairo^dev)"
RSVG_DEV_PATH="$(nix path-info nixpkgs#librsvg^dev)"
PNG_DEV_PATH="$(nix path-info nixpkgs#libpng^dev)"
PNG_LIB_PATH="$(nix path-info nixpkgs#libpng^out)"
GDK_PIXBUF_DEV_PATH="$(nix path-info nixpkgs#gdk-pixbuf^dev)"
ZLIB_DEV_PATH="$(nix path-info nixpkgs#zlib^dev)"
ZLIB_LIB_PATH="$(nix path-info nixpkgs#zlib^out)"

for d in \
  "${FFMPEG_DEV_PATH}/lib/pkgconfig" \
  "${GLIB_DEV_PATH}/lib/pkgconfig" \
  "${CAIRO_DEV_PATH}/lib/pkgconfig" \
  "${RSVG_DEV_PATH}/lib/pkgconfig" \
  "${PNG_DEV_PATH}/lib/pkgconfig" \
  "${GDK_PIXBUF_DEV_PATH}/lib/pkgconfig"; do
  if [[ ! -d "${d}" ]]; then
    echo "Error: pkg-config directory not found: ${d}" >&2
    exit 1
  fi
done

CMAKE_PREFIX_PATH_EXTRA="${FFMPEG_DEV_PATH}:${GLIB_DEV_PATH}:${CAIRO_DEV_PATH}:${RSVG_DEV_PATH}:${PNG_DEV_PATH}:${PNG_LIB_PATH}:${GDK_PIXBUF_DEV_PATH}:${ZLIB_DEV_PATH}:${ZLIB_LIB_PATH}"
CMAKE_LIBRARY_PATH_EXTRA="${PNG_LIB_PATH}/lib:${ZLIB_LIB_PATH}/lib"

# CUDA toolkit in Nix is unfree; allow it for this invocation only.
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure \
  nixpkgs#bash \
  nixpkgs#cmake \
  nixpkgs#ninja \
  nixpkgs#gnuplot \
  nixpkgs#ffmpeg \
  nixpkgs#ffmpeg^dev \
  nixpkgs#pkg-config \
  nixpkgs#gcc \
  nixpkgs#glib \
  nixpkgs#glib^dev \
  nixpkgs#cairo \
  nixpkgs#cairo^dev \
  nixpkgs#librsvg \
  nixpkgs#librsvg^dev \
  nixpkgs#libpng^out \
  nixpkgs#libpng^dev \
  nixpkgs#zlib^out \
  nixpkgs#zlib^dev \
  nixpkgs#gdk-pixbuf \
  nixpkgs#gdk-pixbuf^dev \
  nixpkgs#cudaPackages.cudatoolkit \
  --command bash -lc "export PKG_CONFIG_PATH='${FFMPEG_DEV_PATH}/lib/pkgconfig:${GLIB_DEV_PATH}/lib/pkgconfig:${CAIRO_DEV_PATH}/lib/pkgconfig:${RSVG_DEV_PATH}/lib/pkgconfig:${PNG_DEV_PATH}/lib/pkgconfig:${GDK_PIXBUF_DEV_PATH}/lib/pkgconfig:${ZLIB_DEV_PATH}/lib/pkgconfig:'\"\${PKG_CONFIG_PATH:-}\"; export CMAKE_PREFIX_PATH='${CMAKE_PREFIX_PATH_EXTRA}:'\"\${CMAKE_PREFIX_PATH:-}\"; export CMAKE_LIBRARY_PATH='${CMAKE_LIBRARY_PATH_EXTRA}:'\"\${CMAKE_LIBRARY_PATH:-}\"; export CUDACXX=\"\$(command -v nvcc)\"; export CUDA_HOME=\"\$(dirname \"\$(dirname \"\$CUDACXX\")\")\"; export CUDAToolkit_ROOT=\"\$CUDA_HOME\"; export CMAKE_CUDA_COMPILER=\"\$CUDACXX\"; export CPATH=\"\$CUDA_HOME/include:\${CPATH:-}\"; export C_INCLUDE_PATH=\"\$CUDA_HOME/include:\${C_INCLUDE_PATH:-}\"; export CPLUS_INCLUDE_PATH=\"\$CUDA_HOME/include:\${CPLUS_INCLUDE_PATH:-}\"; SWAPTUBE_SKIP_MICROTEX=1 ./work/render_billiards_visual.sh ${WIDTH} ${HEIGHT} ${FPS} ${MODE} ${COMPUTE} ${PROJECT}"
