#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${REPO_ROOT}"

if [[ ! -f "plot_fractals.py" ]]; then
  echo "Error: plot_fractals.py not found in ${REPO_ROOT}" >&2
  exit 1
fi

nix shell \
  nixpkgs#python3 \
  nixpkgs#python3Packages.numpy \
  nixpkgs#python3Packages.matplotlib \
  nixpkgs#python3Packages.pillow \
  --command bash -lc "python3 plot_fractals.py"
