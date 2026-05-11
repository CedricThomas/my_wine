#!/bin/bash
# Unpack sample game archives into their unpacked/ folders.
#
# Usage:
#   ./scripts/unpack_samples.sh              # unpack all registered samples
#   ./scripts/unpack_samples.sh doom95       # unpack a specific sample
#
# A sample is "registered" when its directory contains a sample.info file.
# Directories without sample.info are silently skipped during auto-discovery.
# Explicit arguments bypass this check (you can always force-unpack).
#
# Each registered sample must have <name>.zip in samples/<name>/.
# The unpacked folders are ignored by git; only archives and docs are tracked.

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

    local dest="$SAMPLES_DIR/unpacked/$name"
    mkdir -p "$dest"
    echo "  $name: unpacking $zip → $dest/"
    unzip -oq "$zip" -d "$dest"
    echo "  $name: done ($(find "$dest" -maxdepth 1 | wc -l) entries)"
}

if [[ $# -gt 0 ]]; then
    # Explicit args — unpack regardless of sample.info
    for name in "$@"; do
        unpack_one "$name"
    done
else
    # Auto-discover only registered samples (those with sample.info)
    for dir in "$SAMPLES_DIR"/*/; do
        name="$(basename "$dir")"
        if [[ -f "$dir/sample.info" ]] && [[ -f "$dir/$name.zip" ]]; then
            unpack_one "$name"
        fi
    done
fi
