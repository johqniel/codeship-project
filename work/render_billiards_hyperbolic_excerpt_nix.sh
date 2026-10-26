#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SWAPTUBE_DIR="${REPO_ROOT}/swaptube"
PROJECT_SRC="${SWAPTUBE_DIR}/src/Projects/Billiards.cpp"
PROJECT_TMP="${SWAPTUBE_DIR}/src/Projects/BilliardsVisualOnly.cpp"

WIDTH="${1:-1280}"
HEIGHT="${2:-720}"
FPS="${3:-30}"
COMPUTE="${4:-CUDA}"
MODE="${5:-render}"

if [[ ! "${WIDTH}" =~ ^[0-9]+$ || ! "${HEIGHT}" =~ ^[0-9]+$ || ! "${FPS}" =~ ^[0-9]+$ ]]; then
  echo "Usage: $0 [width] [height] [fps] [CUDA|HIP|auto] [render|reuse-latest]" >&2
  exit 1
fi

if [[ "${COMPUTE}" != "CUDA" && "${COMPUTE}" != "HIP" && "${COMPUTE}" != "auto" ]]; then
  echo "Error: compute backend must be one of: CUDA, HIP, auto" >&2
  exit 1
fi

if [[ "${MODE}" != "render" && "${MODE}" != "reuse-latest" ]]; then
  echo "Error: mode must be one of: render, reuse-latest" >&2
  exit 1
fi

START_MARKER="...to tile the hyperbolic plane?"
END_MARKER="with music by 6884."
OUT_MP4="${REPO_ROOT}/work/Billiards_hyperbolic_excerpt_${WIDTH}x${HEIGHT}_${FPS}fps.mp4"

build_visual_only_project() {
  if [[ ! -f "${PROJECT_SRC}" ]]; then
    echo "Error: project source not found: ${PROJECT_SRC}" >&2
    exit 1
  fi

  awk '
    /stage_macroblock\(FileBlock\("and Dr\. Lael Edwards-Costa,/{
      print "}";
      exit
    }
    { print }
  ' "${PROJECT_SRC}" > "${PROJECT_TMP}"

  if ! grep -q "void render_video()" "${PROJECT_TMP}"; then
    echo "Error: failed to generate visual-only project file." >&2
    exit 1
  fi
}

find_marker_time() {
  local srt_path="$1"
  local marker="$2"
  awk -v needle="${marker}" 'BEGIN { RS=""; FS="\n" }
    {
      text = ""
      for (i = 3; i <= NF; i++) {
        if (i > 3) text = text "\n"
        text = text $i
      }
      if (index(text, needle) > 0) {
        split($2, t, " --> ")
        print t[1]
        exit
      }
    }
  ' "${srt_path}"
}

srt_to_seconds() {
  local srt_time="$1"
  local hh mm ss ms
  IFS=':,' read -r hh mm ss ms <<< "${srt_time}"
  awk -v h="${hh}" -v m="${mm}" -v s="${ss}" -v ms="${ms}" 'BEGIN { printf "%.3f", (h * 3600) + (m * 60) + s + (ms / 1000.0) }'
}

run_ffmpeg() {
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg "$@"
  else
    nix shell nixpkgs#ffmpeg --command ffmpeg "$@"
  fi
}

if [[ "${MODE}" == "render" ]]; then
  build_visual_only_project
  echo "Rendering Billiards visual-only pipeline with ${WIDTH}x${HEIGHT}@${FPS} (${COMPUTE})..."
  "${REPO_ROOT}/work/render_billiards_visual_nix.sh" "${WIDTH}" "${HEIGHT}" "${FPS}" render "${COMPUTE}" BilliardsVisualOnly
fi

LATEST_OUTPUT="$(ls -1dt "${SWAPTUBE_DIR}/out/BilliardsVisualOnly"/* 2>/dev/null | head -n 1 || true)"
if [[ -z "${LATEST_OUTPUT}" ]]; then
  echo "Error: no BilliardsVisualOnly output folder found under ${SWAPTUBE_DIR}/out/BilliardsVisualOnly" >&2
  exit 1
fi

VIDEO_IN=""
if [[ -f "${LATEST_OUTPUT}/Video.mkv" ]]; then
  VIDEO_IN="${LATEST_OUTPUT}/Video.mkv"
elif [[ -f "${LATEST_OUTPUT}/Video.mp4" ]]; then
  VIDEO_IN="${LATEST_OUTPUT}/Video.mp4"
else
  echo "Error: no rendered video found in ${LATEST_OUTPUT}" >&2
  exit 1
fi

SRT_IN="${LATEST_OUTPUT}/Video.srt"
if [[ ! -f "${SRT_IN}" ]]; then
  echo "Error: subtitle timeline not found: ${SRT_IN}" >&2
  exit 1
fi

START_TIME="$(find_marker_time "${SRT_IN}" "${START_MARKER}")"
END_TIME="$(find_marker_time "${SRT_IN}" "${END_MARKER}")"

if [[ -z "${START_TIME}" ]]; then
  echo "Error: start marker not found in subtitles: ${START_MARKER}" >&2
  exit 1
fi
if [[ -z "${END_TIME}" ]]; then
  echo "Error: end marker not found in subtitles: ${END_MARKER}" >&2
  exit 1
fi

START_SEC="$(srt_to_seconds "${START_TIME}")"
END_SEC="$(srt_to_seconds "${END_TIME}")"

IS_VALID_RANGE="$(awk -v a="${START_SEC}" -v b="${END_SEC}" 'BEGIN { if (b > a) print "1"; else print "0" }')"
if [[ "${IS_VALID_RANGE}" != "1" ]]; then
  echo "Error: invalid clip range. start=${START_TIME}, end=${END_TIME}" >&2
  exit 1
fi

echo "Creating clip from ${START_TIME} to ${END_TIME}..."
run_ffmpeg -y -ss "${START_SEC}" -to "${END_SEC}" -i "${VIDEO_IN}" \
  -map 0:v:0 -an -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart "${OUT_MP4}"

echo "Done. Output clip: ${OUT_MP4}"
echo "Source render folder: ${LATEST_OUTPUT}"
