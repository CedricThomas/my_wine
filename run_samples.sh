#!/usr/bin/env bash
#
# run_samples.sh — Build and/or run Wine native samples.
#
# Usage:
#   ./run_samples.sh           # Build all samples
#   ./run_samples.sh build     # Build all samples
#   ./run_samples.sh run       # Build all + run each under ./my_wine
#   ./run_samples.sh run hello_world  # Build and run a specific sample
#   ./run_samples.sh native hello_world  # Build and run natively (directly)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES_DIR="$SCRIPT_DIR/samples"
BUILDDIR="$SCRIPT_DIR/build"
MY_WINE="$SCRIPT_DIR/my_wine"

# ── Object files to link samples against ────────────────────────
# These are the stubs that implement the Wine API layer for native samples.
# We deliberately exclude loader/syscall/PE-specific objects (entry.o,
# image_mapper.o, import_resolver.o, teb_peb.o, dispatcher.o, etc.) because
# those are only needed for running PE guests under my_wine.
SAMPLE_OBJS="
    $BUILDDIR/ntdll_handle.o $BUILDDIR/ntdll_io.o
    $BUILDDIR/ntdll_memory.o $BUILDDIR/ntdll_process.o $BUILDDIR/ntdll_objects.o
    $BUILDDIR/kernel32.o
    $BUILDDIR/crt_globals.o $BUILDDIR/crt_file.o
    $BUILDDIR/thunk_gen.o $BUILDDIR/signal_handler.o
    $BUILDDIR/native_main.o
"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--Wall -Wextra -O2 -g -I$SCRIPT_DIR -I$SCRIPT_DIR/include}"
LDFLAGS="-lrt -lpthread -lseccomp"

# ── Discover samples ────────────────────────────────────────────
# Each sample is a subdirectory of samples/ containing .c files
discover_samples() {
    local name="$1"
    if [ -n "$name" ]; then
        if [ -d "$SAMPLES_DIR/$name" ]; then
            echo "$name"
        fi
    else
        find "$SAMPLES_DIR" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
    fi
}

# ── Ensure native_main.o exists ────────────────────────────────
ensure_native_main() {
    local obj="$BUILDDIR/native_main.o"
    if [ ! -f "$obj" ]; then
        echo "  CC samples/native_main.c"
        (cd "$SCRIPT_DIR" && make build/native_main.o) 2>&1 || { echo "  FAIL: could not build native_main.o (run 'make samples')"; return 1; }
    fi
}

# ── Build a single sample ───────────────────────────────────────
build_sample() {
    ensure_native_main
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local out="$BUILDDIR/sample_$name"

    # Find all .c files in this sample
    local srcs
    srcs=$(find "$src_dir" -name '*.c' | sort)
    if [ -z "$srcs" ]; then
        echo "  WARN: no .c files in $src_dir, skipping"
        return
    fi

    # Check if rebuild needed
    local need_build=0
    if [ ! -f "$out" ]; then
        need_build=1
    else
        for src in $srcs; do
            if [ "$src" -nt "$out" ]; then
                need_build=1
                break
            fi
        done
    fi

    if [ "$need_build" -eq 0 ]; then
        echo "  OK  $name (up to date)"
        return
    fi

    echo "  CC  $name"
    local objs=""
    for src in $srcs; do
        local base=$(basename "$src" .c)
        local obj="/tmp/sample_${name}_${base}.o"
        $CC $CFLAGS -c "$src" -o "$obj" 2>&1 || { echo "  FAIL $name"; return 1; }
        objs="$objs $obj"
    done

    mkdir -p "$BUILDDIR"
    $CC $CFLAGS -o "$out" $objs $SAMPLE_OBJS $LDFLAGS 2>&1 || { echo "  FAIL $name linking"; return 1; }
    rm -f /tmp/sample_${name}_*.o
    echo "  OK  $name -> $out"
}

# ── Run a sample ─────────────────────────────────────────────────
run_sample() {
    local name="$1"
    local out="$BUILDDIR/sample_$name"
    if [ ! -f "$out" ]; then
        echo "  ERR: $out not built. Run './run_samples.sh build $name' first."
        return 1
    fi
    echo "  RUN $name (native)"
    "$out" "$@"
}

run_sample_under_wine() {
    local name="$1"
    local out="$BUILDDIR/sample_$name"
    if [ ! -f "$out" ]; then
        echo "  ERR: $out not built. Run './run_samples.sh build $name' first."
        return 1
    fi
    if [ ! -f "$MY_WINE" ]; then
        echo "  ERR: $MY_WINE not found. Run 'make' first."
        return 1
    fi
    echo "  RUN $name (under my_wine)"
    "$MY_WINE" "$out" "$@"
}

# ── Main ─────────────────────────────────────────────────────────
MODE="${1:-build}"
TARGET="${2:-}"

case "$MODE" in
    build)
        samples=$(discover_samples "$TARGET")
        if [ -z "$samples" ]; then
            if [ -n "$TARGET" ]; then
                echo "ERR: sample '$TARGET' not found in $SAMPLES_DIR/"
                exit 1
            fi
            echo "No samples found in $SAMPLES_DIR/"
            exit 0
        fi
        for name in $samples; do
            build_sample "$name"
        done
        ;;
    run)
        samples=$(discover_samples "$TARGET")
        for name in $samples; do
            build_sample "$name"
            echo ""
            run_sample "$name" "${@:3}"
        done
        ;;
    native)
        samples=$(discover_samples "$TARGET")
        for name in $samples; do
            build_sample "$name"
            echo ""
            run_sample "$name" "${@:3}"
        done
        ;;
    wine)
        samples=$(discover_samples "$TARGET")
        for name in $samples; do
            build_sample "$name"
            echo ""
            run_sample_under_wine "$name" "${@:3}"
        done
        ;;
    *)
        echo "Usage: $0 {build|run|native|wine} [sample_name] [args...]"
        echo ""
        echo "  build         Build all samples (or named sample)"
        echo "  run           Build + run natively"
        echo "  native        Build + run natively (same as run)"
        echo "  wine          Build + run under ./my_wine"
        exit 1
        ;;
esac
