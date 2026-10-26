#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SWAPTUBE_DIR="${REPO_ROOT}/swaptube"

if [[ ! -d "${SWAPTUBE_DIR}" ]]; then
  echo "Error: could not find swaptube directory at ${SWAPTUBE_DIR}" >&2
  exit 1
fi

if [[ ! -f "${SWAPTUBE_DIR}/go.sh" ]]; then
  echo "Error: ${SWAPTUBE_DIR}/go.sh does not exist." >&2
  exit 1
fi

WIDTH="${1:-1280}"
HEIGHT="${2:-720}"
FPS="${3:-30}"
MODE="${4:-render}"
COMPUTE="${5:-auto}"
PROJECT="${6:-Billiards}"

if [[ ! "${WIDTH}" =~ ^[0-9]+$ || ! "${HEIGHT}" =~ ^[0-9]+$ || ! "${FPS}" =~ ^[0-9]+$ ]]; then
  echo "Usage: $0 [width] [height] [fps] [render|smoketest]" >&2
  echo "Example: $0 1280 720 30 render" >&2
  exit 1
fi

if [[ "${MODE}" != "render" && "${MODE}" != "smoketest" ]]; then
  echo "Error: mode must be 'render' or 'smoketest'." >&2
  echo "Usage: $0 [width] [height] [fps] [render|smoketest] [auto|CUDA|HIP]" >&2
  exit 1
fi

if [[ "${COMPUTE}" != "auto" && "${COMPUTE}" != "CUDA" && "${COMPUTE}" != "HIP" ]]; then
  echo "Error: compute backend must be one of: auto, CUDA, HIP." >&2
  echo "Usage: $0 [width] [height] [fps] [render|smoketest] [auto|CUDA|HIP]" >&2
  exit 1
fi

cd "${SWAPTUBE_DIR}"

if [[ "${SWAPTUBE_SKIP_MICROTEX:-0}" == "1" ]]; then
  MICROTEX_BIN="../MicroTeX-master/build/LaTeX"
  if [[ ! -x "${MICROTEX_BIN}" ]]; then
    mkdir -p "../MicroTeX-master/build"
    cat > "${MICROTEX_BIN}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

output=""
for arg in "$@"; do
  case "$arg" in
    -output=*)
      output="${arg#-output=}"
      ;;
  esac
done

if [[ -n "$output" ]]; then
  mkdir -p "$(dirname "$output")"
  cat > "$output" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="2" height="2">
  <rect width="2" height="2" fill="none"/>
</svg>
SVG
fi
EOF
    chmod +x "${MICROTEX_BIN}"
  fi
fi

# If this repo was moved, CMake can fail with source path mismatch.
# Clean only cache artifacts so go.sh can reconfigure safely.
CACHE_FILE="build/CMakeCache.txt"
if [[ -f "${CACHE_FILE}" ]]; then
  CACHED_SRC_DIR="$(grep '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${CACHE_FILE}" | cut -d= -f2- || true)"
  CURRENT_SRC_DIR="${SWAPTUBE_DIR}"
  if [[ -n "${CACHED_SRC_DIR}" && "${CACHED_SRC_DIR}" != "${CURRENT_SRC_DIR}" ]]; then
    echo "Detected stale CMake cache (${CACHED_SRC_DIR}); cleaning cache artifacts."
    rm -f build/CMakeCache.txt build/build.ninja build/cmake_install.cmake
    rm -rf build/CMakeFiles
  fi
fi

CMD=("bash" "./go.sh" "${PROJECT}" "${WIDTH}" "${HEIGHT}" "${FPS}" "-a")
if [[ "${MODE}" == "smoketest" ]]; then
  CMD+=("-s")
fi
if [[ "${COMPUTE}" != "auto" ]]; then
  CMD+=("-c" "${COMPUTE}")
fi

echo "Running visual-only ${PROJECT} pipeline..."
echo "Command: ${CMD[*]}"
SWAPTUBE_SKIP_MICROTEX=1 "${CMD[@]}"

if [[ "${MODE}" == "render" ]]; then
  if [[ -d "out/${PROJECT}" ]]; then
    LATEST_OUTPUT="$(ls -1dt "out/${PROJECT}"/* 2>/dev/null | head -n 1 || true)"
    if [[ -n "${LATEST_OUTPUT}" ]]; then
      echo "Render complete. Latest output folder: ${LATEST_OUTPUT}"
    fi
  fi
else
  echo "Smoketest complete. No encoded video was produced by design."
fi
