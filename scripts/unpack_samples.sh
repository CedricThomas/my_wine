#!/bin/bash
# Unpack sample game archives into their unpacked/ folders.
#
# Usage:
#   ./scripts/unpack_samples.sh          # unpack all samples
#   ./scripts/unpack_samples.sh doom95   # unpack a specific sample
#
# Each sample archive (<name>.zip) inside samples/<name>/ is extracted
# into samples/<name>/unpacked/. The unpacked folders are ignored by git;
# only the archives and docs are tracked.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SAMPLES_DIR="$PROJECT_DIR/samples"

unpack_one() {
    local name="$1"
    local dir="$SAMPLES_DIR/$name"
    local zip="$dir/$name.zip"

    if [[ ! -f "$zip" ]]; then
        echo "  $name: no $zip found, skipping"
        return
    fi

    local dest="$dir/unpacked"
    mkdir -p "$dest"
    echo "  $name: unpacking $zip → $dest/"
    unzip -oq "$zip" -d "$dest"
    echo "  $name: done ($(find "$dest" -maxdepth 1 | wc -l) entries)"
}

if [[ $# -gt 0 ]]; then
    for name in "$@"; do
        unpack_one "$name"
    done
else
    # Discover all sample dirs that contain a matching .zip
    for dir in "$SAMPLES_DIR"/*/; do
        name="$(basename "$dir")"
        [[ -f "$dir/$name.zip" ]] && unpack_one "$name"
    done
fi
