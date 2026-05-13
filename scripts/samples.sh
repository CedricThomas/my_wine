#!/usr/bin/env bash
#
# samples.sh — Convention-driven build and run for my_wine samples.
#
# Conventions (no hardcoded sample names):
#   - Discover samples: every subdirectory of samples/
#   - EXE build: .c files in the sample dir → {sample_name}.exe
#   - DLL build: .c + .def files in dlls/ subdirectory → parent sample dir
#   - A sample with dlls/ builds BOTH its DLLs and its EXE (if .c exists)
#   - Run mode: reads expected exit code / timeout from sample.info
#   - Samples with no .c in the main dir (DLL-producers only) are SKIPPED
#
# Usage:
#   ./samples.sh              # Build all samples
#   ./samples.sh build        # Build all samples
#   ./samples.sh build NAME   # Build one sample
#   ./samples.sh run          # Build all + run all
#   ./samples.sh run NAME     # Build + run one sample
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
MY_WINE="$PROJECT_DIR/my_wine"
IMAGE_NAME="my_wine-samples"

# ── Helpers ───────────────────────────────────────────────────────

# Parse a key=value entry from a sample.info file.
# Returns the value or empty string if key not found.
parse_sample_info() {
    local file="$1"
    local key="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d= -f2 | tr -d '\r'
}

# Discover sample directory names (sorted).
# With argument: echo the name if it exists as a sample dir.
# Without argument: echo every sample dir name, one per line, sorted.
discover_samples() {
    local name="${1:-}"
    if [ -n "$name" ]; then
        if [ -d "$SAMPLES_DIR/$name" ]; then
            echo "$name"
        fi
    else
        find "$SAMPLES_DIR" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
    fi
}

# Ensure the Docker cross-compiler image exists; build if missing.
ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "  Building Docker image $IMAGE_NAME ..."
        DOCKER_BUILDKIT=0 docker build -t "$IMAGE_NAME" "$PROJECT_DIR" 2>&1 || {
            echo "  FAIL: Docker build failed"
            return 1
        }
    fi
}

# Select MinGW compiler based on arch field in sample.info (default: 64).
# Args: $1 = sample source directory
# Returns: compiler name via stdout
select_compiler() {
    local src_dir="$1"
    local info="$src_dir/sample.info"
    local arch="64"
    if [ -f "$info" ]; then
        arch=$(parse_sample_info "$info" "arch")
    fi
    if [ "$arch" = "32" ]; then
        echo "i686-w64-mingw32-gcc"
    else
        echo "x86_64-w64-mingw32-gcc"
    fi
}

# Rewrite a host path to its /project/... equivalent inside the Docker container.
to_container_path() {
    local host_path="$1"
    echo "/project${host_path#$PROJECT_DIR}"
}

# ── Build: DLLs ───────────────────────────────────────────────────

# Build all DLLs from a sample's dlls/ subdirectory.
# Each .def file produces a .dll in the parent (sample) directory.
# Sources: .c files in dlls/ with the same base name as the .def.
build_dlls() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local dll_dir="$src_dir/dlls"

    # No dlls/ directory → nothing to do
    [ -d "$dll_dir" ] || return 0

    local defs
    defs=$(find "$dll_dir" -name '*.def' 2>/dev/null | sort)
    [ -z "$defs" ] && return 0

    ensure_image

    for def in $defs; do
        local dll_base
        dll_base=$(basename "$def" .def)
        local out_dll="$src_dir/${dll_base}.dll"

        # Find the matching .c source (same base name)
        local c_src="$dll_dir/${dll_base}.c"
        [ -f "$c_src" ] || continue

        # Check if rebuild is needed (compare timestamps)
        local need_build=0
        if [ ! -f "$out_dll" ]; then
            need_build=1
        else
            for src in "$c_src" "$def"; do
                if [ "$src" -nt "$out_dll" ]; then
                    need_build=1
                    break
                fi
            done
        fi

        [ "$need_build" -eq 0 ] && continue

        local container_src
        local container_def
        container_src=$(to_container_path "$c_src")
        container_def=$(to_container_path "$def")

        # Select compiler based on arch field in sample.info (default: 64)
        local CC
        CC=$(select_compiler "$src_dir")

        echo "  CC  ${name}/dlls/${dll_base}.dll (mingw-dll)"
        docker run --rm \
            -v "$PROJECT_DIR:/project:ro" \
            -v "$src_dir:/out" \
            "$IMAGE_NAME" \
            "$CC" \
            -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O2 -shared \
            -Wl,"$container_def" \
            -o "/out/${dll_base}.dll" \
            "$container_src" 2>&1 || {
                echo "  FAIL ${name}/dlls/${dll_base}.dll"
                return 1
            }

        echo "  OK  $name -> ${dll_base}.dll"
    done
}

# ── Build: EXE ────────────────────────────────────────────────────

# Build the EXE from .c files in the sample's main directory (not dlls/).
# Output: {sample_name}.exe in the same directory.
build_exe() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local out_exe="$src_dir/${name}.exe"

    # Find .c files in the sample dir only (maxdepth 1 excludes dlls/)
    local srcs
    srcs=$(find "$src_dir" -maxdepth 1 -name '*.c' 2>/dev/null | sort)
    [ -z "$srcs" ] && return 0

    # Check if rebuild is needed
    local need_build=0
    if [ ! -f "$out_exe" ]; then
        need_build=1
    else
        for src in $srcs; do
            if [ "$src" -nt "$out_exe" ]; then
                need_build=1
                break
            fi
        done
    fi

    [ "$need_build" -eq 0 ] && return 0

    ensure_image

    # Rewrite host paths to container paths
    local container_srcs=""
    for src in $srcs; do
        container_srcs="$container_srcs $(to_container_path "$src")"
    done

    # Select compiler based on arch field in sample.info (default: 64)
    local CC
    CC=$(select_compiler "$src_dir")

    # Read optimization level from sample.info (default: 2)
    local OPT_LEVEL="2"
    if [ -f "$src_dir/sample.info" ]; then
        local opt_val
        opt_val=$(parse_sample_info "$src_dir/sample.info" "optimize")
        if [[ "$opt_val" =~ ^[012]$ ]]; then
            OPT_LEVEL="$opt_val"
        fi
    fi

    # 32-bit MinGW-w64 enables -fstack-protector-strong by default, which crashes
    # at -O2 when EBP is used as a data register. Disable it for 32-bit builds.
    local EXTRA_FLAGS=""
    if [ "$CC" = "i686-w64-mingw32-gcc" ]; then
        EXTRA_FLAGS="-fno-stack-protector"
    fi

    echo "  CC  $name (mingw)"
    docker run --rm \
        -v "$PROJECT_DIR:/project:ro" \
        -v "$src_dir:/out" \
        "$IMAGE_NAME" \
        "$CC" \
        -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O${OPT_LEVEL} -mconsole \
        $EXTRA_FLAGS \
        -o "/out/${name}.exe" \
        $container_srcs 2>&1 || {
            echo "  FAIL $name"
            return 1
        }

    echo "  OK  $name -> ${name}.exe"
}

# Build a single sample: DLLs (if dlls/ exists) + EXE (if .c files in main dir).
build_sample() {
    local name="$1"
    build_dlls "$name"
    build_exe "$name"
}

# ── Run ───────────────────────────────────────────────────────────

# Check if a sample has .c files in its main directory (not in dlls/).
has_main_c_files() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    find "$src_dir" -maxdepth 1 -name '*.c' 2>/dev/null | head -1 | grep -q .
}

# Run a single sample under my_wine.
# Returns: 0 = PASS, 1 = FAIL, 2 = SKIP
run_sample() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local exe="$src_dir/${name}.exe"
    local info="$src_dir/sample.info"

    # Skip samples with no .c in the main dir
    if ! has_main_c_files "$name"; then
        echo "  SKIP  $name (no .c in main dir sample)"
        return 2
    fi

    if [ ! -f "$exe" ]; then
        echo "  ERR: $exe not built. Run './scripts/samples.sh build $name' first."
        return 1
    fi

    if [ ! -f "$MY_WINE" ]; then
        echo "  ERR: $MY_WINE not found. Run 'make' first."
        return 1
    fi

    # Read expected exit code and timeout from sample.info (with defaults)
    local expected_exit=0
    local timeout_sec=5
    if [ -f "$info" ]; then
        expected_exit=$(parse_sample_info "$info" "exit")
        timeout_sec=$(parse_sample_info "$info" "timeout")
    fi

    echo "  RUN $name (under my_wine, expect exit=$expected_exit, timeout=${timeout_sec}s)"

    # Run with timeout; capture exit code without triggering set -e
    # Suppress my_wine debug logs (DBG_*) on stderr unless DEBUG is set
    local ret=0
    if [ -n "${DEBUG:-}" ]; then
        timeout "$timeout_sec" "$MY_WINE" "$exe" || ret=$?
    else
        timeout "$timeout_sec" "$MY_WINE" "$exe" 2>/dev/null || ret=$?
    fi

    if [ "$ret" -eq "$expected_exit" ]; then
        echo "  PASS  $name (exit=$ret, expected=$expected_exit)"
        return 0
    else
        echo "  FAIL  $name (exit=$ret, expected=$expected_exit)"
        return 1
    fi
}

# ── Main ──────────────────────────────────────────────────────────

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
            build_sample "$name" || { echo "  FAIL  $name (build failed)"; exit 1; }
        done
        ;;

    run)
        samples=$(discover_samples "$TARGET")
        if [ -z "$samples" ]; then
            if [ -n "$TARGET" ]; then
                echo "ERR: sample '$TARGET' not found in $SAMPLES_DIR/"
                exit 1
            fi
            echo "No samples found in $SAMPLES_DIR/"
            exit 0
        fi
        pass=0 fail=0 skip=0
        for name in $samples; do
            # Build the sample first
            if ! build_sample "$name"; then
                echo "  FAIL  $name (build failed, not running)"
                fail=$((fail + 1))
                continue
            fi

            # Run — use || to prevent set -e from aborting on expected failures
            result=0
            run_sample "$name" || result=$?
            case $result in
                0) pass=$((pass + 1)) ;;
                2) skip=$((skip + 1)) ;;
                *) fail=$((fail + 1)) ;;
            esac
        done
        echo ""
        echo "Results: $pass passed, $fail failed, $skip skipped"
        if [ "$fail" -gt 0 ]; then
            exit 1
        fi
        ;;

    *)
        echo "Usage: $0 {build|run} [sample_name]"
        echo ""
        echo "  build         Build all samples (or named sample)"
        echo "  run           Build + run under ./my_wine"
        exit 1
        ;;
esac
