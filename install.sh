#!/bin/bash
# Quick start installer for macOS: builds the plugin against the installed
# OBS Studio and installs it for the current user.
#
#   curl -fsSL https://raw.githubusercontent.com/realworldbuilder/obs-image-reaction/main/install.sh | bash
set -euo pipefail

REPO="https://github.com/realworldbuilder/obs-image-reaction.git"

if [[ "$(uname)" != "Darwin" ]]; then
  echo "This installer is for macOS only. See the README for other platforms." >&2
  exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
  echo "The Xcode Command Line Tools are required. Install them with:" >&2
  echo "  xcode-select --install" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "Downloading obs-image-reaction..."
git clone --quiet --depth 1 "${REPO}" "${WORK_DIR}/obs-image-reaction"
"${WORK_DIR}/obs-image-reaction/build-macos-local.sh" --install
