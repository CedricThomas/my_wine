#!/usr/bin/env bash
#
# run_samples.sh — Unified sample scenario runner.
#
# Dispatches console samples to samples.sh and graphical samples to
# graphical_samples.sh (which uses Xvfb inside Docker).
#
# Usage:
#   scripts/run_samples.sh [NAME]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"

parse_sample_info() {
    grep "^${1}=" "$2" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true
}

sample_type() {
    local info="$SAMPLES_DIR/$1/sample.info"
    [ -f "$info" ] && parse_sample_info type "$info"
}

if [ "${1:-}" ]; then
    TARGET="$1"
    [ ! -d "$SAMPLES_DIR/$TARGET" ] && { echo "ERR: sample '$TARGET' not found"; exit 1; }
    if [ "$(sample_type "$TARGET")" = "graphical" ]; then
        exec "$SCRIPT_DIR/graphical_samples.sh" run "$TARGET"
    fi
    exec "$SCRIPT_DIR/samples.sh" run "$TARGET"
fi

fail=0

# Console samples
has_console=0
for dir in "$SAMPLES_DIR"/*/; do
    name="$(basename "$dir")"
    [ "$(sample_type "$name")" != "graphical" ] && { has_console=1; break; }
done

if [ "$has_console" -eq 1 ]; then
    echo "==== Running console sample scenarios ===="
    MY_WINE_SKIP_GRAPHICAL_SAMPLES_SILENT=1 "$SCRIPT_DIR/samples.sh" run || fail=$((fail + 1))
fi

# Graphical samples
graphical=$("$SCRIPT_DIR/graphical_samples.sh" list 2>/dev/null)
if [ -n "$graphical" ]; then
    echo "==== Running graphical sample scenarios ===="
    "$SCRIPT_DIR/graphical_samples.sh" run || fail=$((fail + 1))
fi

if [ "$fail" -gt 0 ]; then
    echo "Unified sample scenarios: $fail failed"
    exit 1
fi

echo "Unified sample scenarios: all passed"
