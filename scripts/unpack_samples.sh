#!/bin/bash
# Unpack sample game archives into their respective unpacked/ directories.
# Usage: ./scripts/unpack_samples.sh          # unpack all
#        ./scripts/unpack_samples.sh doom95   # unpack only doom95

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

unpack_one() {
    local name="$1"
    local dir="$REPO_DIR/$name"
    local zip="$dir/$name.zip"

    if [[ ! -f "$zip" ]]; then
        echo "  $name: no $zip found, skipping"
        return
    fi

    local unpacked="$dir/unpacked"
    mkdir -p "$unpacked"
    echo "  $name: unpacking $zip → $unpacked/"
    unzip -oq "$zip" -d "$unpacked"
    echo "  $name: done ($(ls "$unpacked" | wc -l) files)"
}

if [[ $# -gt 0 ]]; then
    for name in "$@"; do
        unpack_one "$name"
    done
else
    # Discover all dirs with matching .zip files
    for dir in "$REPO_DIR"/*/; do
        name="$(basename "$dir")"
        if [[ -f "$dir/$name.zip" ]]; then
            unpack_one "$name"
        fi
    done
fi
