#!/usr/bin/env bash
#
# run_samples.sh — Unified sample scenario runner.
#
# Native binaries under tests/ are unit-style checks. Samples are e2e scenario
# programs. This runner dispatches console samples to scripts/samples.sh and
# graphical samples to scripts/graphical_samples.sh, where Xvfb and
# applied_inputs.txt are used.
#
# Usage:
#   scripts/run_samples.sh [NAME]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
TARGET="${1:-}"

parse_sample_info() {
    local file="$1"
    local key="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r'
}

sample_type() {
    local name="$1"
    local info="$SAMPLES_DIR/$name/sample.info"
    if [ -f "$info" ]; then
        parse_sample_info "$info" "type"
    fi
}

discover_samples() {
    find "$SAMPLES_DIR" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
}

if [ -n "$TARGET" ]; then
    if [ ! -d "$SAMPLES_DIR/$TARGET" ]; then
        echo "ERR: sample '$TARGET' not found in $SAMPLES_DIR/"
        exit 1
    fi

    if [ "$(sample_type "$TARGET")" = "graphical" ]; then
        exec "$SCRIPT_DIR/graphical_samples.sh" run "$TARGET"
    fi
    exec "$SCRIPT_DIR/samples.sh" run "$TARGET"
fi

fail=0
has_console=0
while IFS= read -r name; do
    if [ "$(sample_type "$name")" != "graphical" ]; then
        has_console=1
        break
    fi
done < <(discover_samples)

if [ "$has_console" -eq 1 ]; then
    echo "==== Running console sample scenarios ===="
    if ! MY_WINE_SKIP_GRAPHICAL_SAMPLES_SILENT=1 "$SCRIPT_DIR/samples.sh" run; then
        fail=$((fail + 1))
    fi
fi

_graphical_list=$("$SCRIPT_DIR/graphical_samples.sh" list 2>/dev/null)
if [ -n "$_graphical_list" ]; then
    echo "==== Running graphical sample scenarios ===="
    if ! "$SCRIPT_DIR/graphical_samples.sh" run; then
        fail=$((fail + 1))
    fi
fi

if [ "$fail" -gt 0 ]; then
    echo "Unified sample scenarios: $fail failed"
    exit 1
fi

echo "Unified sample scenarios: all passed"
