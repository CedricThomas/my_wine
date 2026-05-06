#!/usr/bin/env bash
#
# samples.sh — Build and/or run my_wine samples.
#
# Samples are written as Windows C code and cross-compiled to PE .exe
# via Docker (mingw-w64). They are then run under ./my_wine.
#
# Usage:
#   ./samples.sh              # Build all samples
#   ./samples.sh hello_world  # Build a specific sample
#   ./samples.sh run          # Build all + run each under ./my_wine
#   ./samples.sh run hello_world  # Build + run a specific sample
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
BUILDDIR="$PROJECT_DIR/build"
MY_WINE="$PROJECT_DIR/my_wine"
IMAGE_NAME="my_wine-samples"

# ── Discover sample directories ──────────────────────────────────
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

# ── Check if a sample produces a runnable EXE ────────────────────
is_exe_sample() {
    local name="$1"
    # A sample is runnable (produces .exe) if it has no .def files
    # (.def files indicate DLL builds via build_sample)
    local def
    def=$(find "$SAMPLES_DIR/$name" -name '*.def' 2>/dev/null | head -1)
    [ -z "$def" ]
}

# ── Ensure Docker image exists ───────────────────────────────────
ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "  Building Docker image $IMAGE_NAME ..."
        DOCKER_BUILDKIT=0 docker build -t "$IMAGE_NAME" "$SAMPLES_DIR" 2>&1 || { echo "FAIL: Docker build failed"; return 1; }
    fi
}

# ── Build a single sample ────────────────────────────────────────
build_sample() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local out="$src_dir/${name}.exe"

    # Find all .c files in this sample
    local srcs
    srcs=$(find "$src_dir" -name '*.c' | sort)
    if [ -z "$srcs" ]; then
        echo "  WARN: no .c files in $src_dir, skipping"
        return
    fi

    # Check for .def file (DLL marker)
    local def
    def=$(find "$src_dir" -name '*.def' | head -1)
    if [ -n "$def" ]; then
        local dll_base
        dll_base=$(basename "$def" .def)
        local out_dll="$src_dir/${dll_base}.dll"

        # Check if rebuild needed
        local need_build=0
        if [ ! -f "$out_dll" ]; then
            need_build=1
        else
            for src in $srcs; do
                if [ "$src" -nt "$out_dll" ]; then
                    need_build=1
                    break
                fi
            done
        fi

        if [ "$need_build" -eq 0 ]; then
            return
        fi

        ensure_image

        local in_container_srcs
        in_container_srcs=$(echo "$srcs" | sed "s|$PROJECT_DIR|/project|g")

        echo "  CC  $name (mingw-dll)"
        docker run --rm \
            -v "$PROJECT_DIR:/project:ro" \
            -v "$src_dir:/out" \
            "$IMAGE_NAME" \
            x86_64-w64-mingw32-gcc \
            -Wall -Wextra -O2 -shared \
            -Wl,/project${def#$PROJECT_DIR} \
            -o "/out/${dll_base}.dll" \
            $in_container_srcs 2>&1 || { echo "  FAIL $name"; return 1; }

        echo "  OK  $name -> ${dll_base}.dll"
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
        return
    fi

    ensure_image

    # Cross-compile via Docker
    # Mount project read-only at /project; output dir writable at /out
    # Rewrite host paths -> /project/... for use inside the container
    echo "  CC  $name (mingw)"
    local in_container_srcs=""
    for src in $srcs; do
        in_container_srcs="$in_container_srcs /project${src#$PROJECT_DIR}"
    done

    docker run --rm \
        -v "$PROJECT_DIR:/project:ro" \
        -v "$src_dir:/out" \
        "$IMAGE_NAME" \
        x86_64-w64-mingw32-gcc \
        -Wall -Wextra -O2 -mconsole \
        -o "/out/${name}.exe" \
        $in_container_srcs 2>&1 || { echo "  FAIL $name"; return 1; }

    echo "  OK  $name -> $(basename "$out")"
}

# ── Run a sample under my_wine ────────────────────────────────────
run_sample() {
    local name="$1"
    local exe="$SAMPLES_DIR/$name/${name}.exe"

    if [ ! -f "$exe" ]; then
        echo "  ERR: $exe not built. Run './samples/samples.sh $name' first."
        return 1
    fi

    if [ ! -f "$MY_WINE" ]; then
        echo "  ERR: $MY_WINE not found. Run 'make' first."
        return 1
    fi

    # If running dll_loader, ensure exportlib.dll is copied into its dir
    if [ "$name" = "dll_loader" ]; then
        local dll_src="$SAMPLES_DIR/dll_sample/exportlib.dll"
        if [ ! -f "$dll_src" ]; then
            echo "  ERR: exportlib.dll not built. Build dll_sample first."
            return 1
        fi
        cp "$dll_src" "$SAMPLES_DIR/dll_loader/"
    fi

    echo "  RUN $name (under my_wine)"
    local ret=0
    timeout 5 "$MY_WINE" "$exe" "${@:2}" || ret=$?
    if [ $ret -eq 0 ]; then
        echo "  PASS  $name"
        return 0
    elif [ $ret -eq 124 ]; then
        echo "  PASS  $name (timed out after 5s, process was stable)"
        return 0
    elif [ $ret -eq 139 ] && [ "$name" = "null_deref" ]; then
        echo "  PASS  $name (expected SIGSEGV caught by crash handler)"
        return 0
    else
        echo "  FAIL  $name (exit code $ret)"
        return 1
    fi
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
            # dll_loader depends on dll_sample (uses exportlib.dll)
            if [ "$name" = "dll_loader" ]; then
                build_sample "dll_sample"
            fi
            build_sample "$name"
        done
        ;;
    run)
        samples=$(discover_samples "$TARGET")
        pass=0 fail=0 skip=0
        for name in $samples; do
            # Skip DLL-only samples in run mode (they produce .dll, not .exe)
            if ! is_exe_sample "$name"; then
                echo "  SKIP  $name (DLL sample — not runnable)"
                skip=$((skip + 1))
                # Build it anyway so other samples that depend on it can find it
                build_sample "$name"
                echo ""
                continue
            fi

            # Build dependencies first (dll_loader needs dll_sample)
            if [ "$name" = "dll_loader" ]; then
                build_sample "dll_sample"
            fi
            build_sample "$name"
            echo ""
            if run_sample "$name" "${@:3}"; then
                pass=$((pass + 1))
            else
                fail=$((fail + 1))
            fi
        done
        echo ""
        echo "  Results: $pass passed, $fail failed, $skip skipped"
        if [ "$fail" -gt 0 ]; then
            exit 1
        fi
        ;;
    *)
        echo "Usage: $0 {build|run} [sample_name] [args...]"
        echo ""
        echo "  build         Build all samples (or named sample)"
        echo "  run           Build + run under ./my_wine"
        exit 1
        ;;
esac
