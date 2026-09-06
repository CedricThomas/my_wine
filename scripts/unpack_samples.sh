#!/usr/bin/env bash
#
# unpack_samples.sh — Unpack sample game archives into samples/unpacked/.
#
# Usage:
#   ./scripts/unpack_samples.sh              # unpack all registered samples
#   ./scripts/unpack_samples.sh doom95       # unpack a specific sample
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"

parse_sample_info() {
    grep "^${1}=" "$2" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true
}

unpack_one() {
    local name="$1" dir="$SAMPLES_DIR/$1"
    local info="$dir/sample.info"
    [ ! -f "$info" ] && { echo "  $name: no sample.info, skipping"; return; }

    local zip_name
    zip_name="$(parse_sample_info archive "$info")"
    [ -z "$zip_name" ] && { echo "  $name: no archive= in sample.info, skipping"; return; }

    local zip="$dir/$zip_name"
    [ ! -f "$zip" ] && { echo "  $name: archive $zip not found, skipping"; return; }

    local dest="$SAMPLES_DIR/unpacked/$name"
    mkdir -p "$dest"
    echo "  $name: unpacking $zip → $dest/"
    unzip -oq "$zip" -d "$dest"
    echo "  $name: done ($(find "$dest" -maxdepth 1 | wc -l) entries)"
}

if [ $# -gt 0 ]; then
    for name in "$@"; do unpack_one "$name"; done
else
    for dir in "$SAMPLES_DIR"/*/; do
        name="$(basename "$dir")"
        [ -f "$dir/sample.info" ] && unpack_one "$name"
    done
fi
